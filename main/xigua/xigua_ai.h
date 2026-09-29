#ifndef XIGUA_AI_H
#define XIGUA_AI_H
#include "xigua_ai_protocol.h"
#include "xigua_config.h"

/* Blocking service-worker-only call. One text request, no retries or tools.
 * Requires connected Wi-Fi and a valid system clock for TLS certificates.
 * The result never contains a server error body or a credential.
 */
bool xigua_ai_request(const xigua_ai_config_t *config, const char *prompt,
                      xigua_ai_result_t *out);
#endif
