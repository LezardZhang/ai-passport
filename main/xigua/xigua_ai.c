#include "xigua_ai.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_parser.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUEST_DEADLINE_US (30000000LL)
#define RESPONSE_HEADER_MAX 4096
#define RESPONSE_WIRE_MAX (XG_AI_RESPONSE_MAX + RESPONSE_HEADER_MAX + 8192)
#define IO_CHUNK 512

typedef struct {
    char *body;
    size_t length;
    bool headers_complete;
    bool complete;
    xigua_ai_status_t status;
    int http_status;
} response_t;

static bool expired(int64_t deadline) { return esp_timer_get_time() >= deadline; }
static void yield_io(void) { vTaskDelay(pdMS_TO_TICKS(10) + 1); }
static void clear_private(void *data, size_t length)
{
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}
static bool would_block(ssize_t n)
{
    return n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE;
}

/* esp_tls's async connect still performs synchronous getaddrinfo. Resolve on
 * lwIP's own thread first, so DNS consumes the same deadline as TLS and HTTP.
 * The callback owns a reference: timing out never leaves a dangling stack
 * address, and a late DNS result only releases its small private allocation.
 */
typedef struct {
    atomic_uint references;
    atomic_bool ready;
    char hostname[XG_ENDPOINT_MAX];
    char address[48];
    bool success;
} resolver_t;
static void resolver_release(resolver_t *r)
{
    if (atomic_fetch_sub(&r->references, 1) == 1) free(r);
}
static void resolved(const char *name, const ip_addr_t *address, void *arg)
{
    (void)name;
    resolver_t *r = arg;
    r->success = address && ipaddr_ntoa_r(address, r->address, sizeof(r->address));
    atomic_store_explicit(&r->ready, true, memory_order_release);
    resolver_release(r);
}
static void resolve_on_lwip(void *arg)
{
    resolver_t *r = arg;
    ip_addr_t address;
    err_t error = dns_gethostbyname(r->hostname, &address, resolved, r);
    if (error != ERR_INPROGRESS) resolved(r->hostname, error == ERR_OK ? &address : NULL, r);
}
static xigua_ai_status_t resolve_host(const char *host, char *address, int64_t deadline)
{
    resolver_t *r = calloc(1, sizeof(*r));
    if (!r) return XG_AI_NO_MEMORY;
    atomic_init(&r->references, 2);
    atomic_init(&r->ready, false);
    strcpy(r->hostname, host);
    if (tcpip_try_callback(resolve_on_lwip, r) != ERR_OK) {
        resolver_release(r);
        resolver_release(r);
        return XG_AI_NETWORK_ERROR;
    }
    while (!atomic_load_explicit(&r->ready, memory_order_acquire) && !expired(deadline)) yield_io();
    xigua_ai_status_t status = expired(deadline) ? XG_AI_TIMEOUT :
                               r->success ? XG_AI_OK : XG_AI_NETWORK_ERROR;
    if (status == XG_AI_OK) strcpy(address, r->address);
    resolver_release(r);
    return status;
}
static int headers_complete(http_parser *parser)
{
    response_t *r = parser->data;
    r->headers_complete = true;
    r->http_status = parser->status_code;
    /* Redirects never open another connection or resend the API key. */
    if (parser->status_code != 200) { r->status = XG_AI_HTTP_ERROR; return -1; }
    if (parser->content_length != UINT64_MAX && parser->content_length > XG_AI_RESPONSE_MAX) {
        r->status = XG_AI_RESPONSE_TOO_LARGE;
        return -1;
    }
    return 0;
}
static int response_body(http_parser *parser, const char *data, size_t length)
{
    response_t *r = parser->data;
    if (length > XG_AI_RESPONSE_MAX - r->length) {
        r->status = XG_AI_RESPONSE_TOO_LARGE;
        return -1;
    }
    memcpy(r->body + r->length, data, length);
    r->length += length;
    return 0;
}
static int message_complete(http_parser *parser)
{
    response_t *r = parser->data;
    r->complete = true;
    http_parser_pause(parser, 1);
    return 0;
}

static xigua_ai_status_t write_all(esp_tls_t *tls, const char *data, size_t length,
                                  int64_t deadline)
{
    size_t sent = 0;
    while (sent < length) {
        if (expired(deadline)) return XG_AI_TIMEOUT;
        size_t chunk = length - sent;
        if (chunk > IO_CHUNK) chunk = IO_CHUNK;
        ssize_t n = esp_tls_conn_write(tls, data + sent, chunk);
        if (would_block(n)) { yield_io(); continue; }
        if (n <= 0) return XG_AI_NETWORK_ERROR;
        sent += (size_t)n;
    }
    return expired(deadline) ? XG_AI_TIMEOUT : XG_AI_OK;
}

bool xigua_ai_request(const xigua_ai_config_t *config, const char *prompt,
                      xigua_ai_result_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->status = XG_AI_INVALID_CONFIG;
    if (!xigua_config_ai_valid(config, true)) return false;
    struct http_parser_url url;
    http_parser_url_init(&url);
    if (http_parser_parse_url(config->endpoint, strlen(config->endpoint), 0, &url) ||
        !(url.field_set & (1 << UF_HOST)) || !(url.field_set & (1 << UF_PATH))) return false;
    char host[XG_ENDPOINT_MAX];
    size_t host_len = url.field_data[UF_HOST].len;
    memcpy(host, config->endpoint + url.field_data[UF_HOST].off, host_len);
    host[host_len] = 0;
    int port = url.field_set & (1 << UF_PORT) ? url.port : 443;
    const char *path = config->endpoint + url.field_data[UF_PATH].off;
    size_t path_len = url.field_data[UF_PATH].len;
    /* A single bounded allocation holds outgoing JSON then incoming JSON. */
    char *buffer = calloc(1, XG_AI_RESPONSE_MAX + 1);
    if (!buffer) { out->status = XG_AI_NO_MEMORY; return false; }
    out->status = xigua_ai_encode(config->model, prompt, buffer, XG_AI_REQUEST_MAX);
    if (out->status != XG_AI_OK) {
        clear_private(buffer, XG_AI_RESPONSE_MAX + 1);
        free(buffer);
        return false;
    }
    size_t request_len = strlen(buffer);
    char header[768];
    int header_len = snprintf(header, sizeof(header),
        "POST %.*s HTTP/1.1\r\nHost: %s:%d\r\napi-key: %s\r\n"
        "Content-Type: application/json\r\nAccept: application/json\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",
        (int)path_len, path, host, port, config->key, (unsigned)request_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        out->status = XG_AI_INVALID_CONFIG;
        clear_private(header, sizeof(header));
        clear_private(buffer, XG_AI_RESPONSE_MAX + 1);
        free(buffer);
        return false;
    }
    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        out->status = XG_AI_NO_MEMORY;
        clear_private(header, sizeof(header));
        clear_private(buffer, XG_AI_RESPONSE_MAX + 1);
        free(buffer);
        return false;
    }
    const esp_tls_cfg_t tls_config = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .non_block = true,
        .timeout_ms = 100,
        .common_name = host,
    };
    int64_t deadline = esp_timer_get_time() + REQUEST_DEADLINE_US;
    char address[48];
    out->status = resolve_host(host, address, deadline);
    if (out->status != XG_AI_OK) goto done;
    out->status = XG_AI_NETWORK_ERROR;
    for (;;) {
        if (expired(deadline)) { out->status = XG_AI_TIMEOUT; goto done; }
        int connected = esp_tls_conn_new_async(address, (int)strlen(address), port, &tls_config, tls);
        if (connected < 0) goto done;
        if (connected == 1) break;
        yield_io();
    }
    out->status = write_all(tls, header, (size_t)header_len, deadline);
    if (out->status != XG_AI_OK) goto done;
    out->status = write_all(tls, buffer, request_len, deadline);
    if (out->status != XG_AI_OK) goto done;
    memset(buffer, 0, XG_AI_RESPONSE_MAX + 1);
    response_t response = {.body = buffer, .status = XG_AI_OK};
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data = &response;
    const http_parser_settings settings = {
        .on_headers_complete = headers_complete,
        .on_body = response_body,
        .on_message_complete = message_complete,
    };
    size_t header_bytes = 0, wire_bytes = 0;
    char chunk[IO_CHUNK];
    while (!response.complete) {
        if (expired(deadline)) { out->status = XG_AI_TIMEOUT; goto done; }
        ssize_t n = esp_tls_conn_read(tls, chunk, sizeof(chunk));
        if (would_block(n)) { yield_io(); continue; }
        if (n < 0) { out->status = XG_AI_NETWORK_ERROR; goto done; }
        if (!n) {
            http_parser_execute(&parser, &settings, "", 0);
            if (!response.complete) { out->status = XG_AI_INCOMPLETE; goto done; }
            break;
        }
        wire_bytes += (size_t)n;
        if (wire_bytes > RESPONSE_WIRE_MAX) { out->status = XG_AI_RESPONSE_TOO_LARGE; goto done; }
        size_t consumed = 0;
        while (consumed < (size_t)n && !response.complete) {
            /* Parse headers bytewise to enforce a wire-level header budget,
             * including unterminated headers, before the parser accepts them. */
            size_t take = response.headers_complete ? (size_t)n - consumed : 1;
            if (!response.headers_complete && ++header_bytes > RESPONSE_HEADER_MAX) {
                out->status = XG_AI_RESPONSE_TOO_LARGE;
                goto done;
            }
            size_t parsed = http_parser_execute(&parser, &settings, chunk + consumed, take);
            out->http_status = response.http_status;
            if (response.status != XG_AI_OK) { out->status = response.status; goto done; }
            if (HTTP_PARSER_ERRNO(&parser) != HPE_OK &&
                !(response.complete && HTTP_PARSER_ERRNO(&parser) == HPE_PAUSED)) {
                out->status = XG_AI_INVALID_RESPONSE;
                goto done;
            }
            if (parsed != take && !response.complete) { out->status = XG_AI_INVALID_RESPONSE; goto done; }
            consumed += parsed;
        }
    }
    if (expired(deadline)) { out->status = XG_AI_TIMEOUT; goto done; }
    /* Release TLS record buffers before allocating the cJSON tree. */
    esp_tls_conn_destroy(tls);
    tls = NULL;
    out->status = xigua_ai_decode(buffer, response.length, out);
done:
    if (tls) esp_tls_conn_destroy(tls);
    /* Do not leave the API key or user prompt in reusable stack/heap storage. */
    clear_private(header, sizeof(header));
    clear_private(buffer, XG_AI_RESPONSE_MAX + 1);
    free(buffer);
    return out->status == XG_AI_OK;
}
