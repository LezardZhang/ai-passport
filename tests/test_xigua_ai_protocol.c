#include "xigua_ai_protocol.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static xigua_ai_result_t result;
static char wire[XG_AI_RESPONSE_MAX + 32];
static xigua_ai_status_t decode(const char *body)
{
    memset(&result, 0xa5, sizeof(result));
    xigua_ai_status_t status = xigua_ai_decode(body, strlen(body), &result);
    assert(status == result.status);
    if (status != XG_AI_OK) assert(result.answer[0] == 0);
    return status;
}
static void test_request(void)
{
    const char *prompt = "Quotes: \" \\ \n\t\r \b \f \x01 UTF8: \xe4\xbd\xa0\xe5\xa5\xbd";
    assert(xigua_ai_encode("mimo-v2.6-flash", prompt, wire, sizeof(wire)) == XG_AI_OK);
    cJSON *root = cJSON_Parse(wire);
    assert(root);
    const cJSON *messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(messages->child, "content");
    assert(!strcmp(content->valuestring, prompt));
    assert(cJSON_GetObjectItemCaseSensitive(root, "max_completion_tokens")->valueint == 256);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "stream")));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(root, "thinking"), "type")->valuestring, "disabled"));
    assert(!cJSON_GetObjectItemCaseSensitive(root, "tools"));
    cJSON_Delete(root);
    assert(xigua_ai_encode("m", " \n", wire, sizeof(wire)) == XG_AI_INVALID_PROMPT);
    assert(xigua_ai_encode("m", "\xc0\xaf", wire, sizeof(wire)) == XG_AI_INVALID_PROMPT);
    assert(xigua_ai_encode("m", "ok", wire, 2) == XG_AI_NO_MEMORY && !wire[0]);
    char long_prompt[514];
    memset(long_prompt, 'x', sizeof(long_prompt));
    long_prompt[513] = 0;
    assert(xigua_ai_encode("m", long_prompt, wire, sizeof(wire)) == XG_AI_INVALID_PROMPT);
    long_prompt[512] = 0;
    assert(xigua_ai_encode("m", long_prompt, wire, sizeof(wire)) == XG_AI_OK);
    char escaped[513];
    memset(escaped, 1, 512); escaped[512] = 0;
    assert(xigua_ai_encode("m", escaped, wire, sizeof(wire)) == XG_AI_OK);
    assert(strlen(wire) < XG_AI_REQUEST_MAX);
}
static void test_response(void)
{
    const char *good = "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"hello \\u4f60\\u597d \\ud83d\\ude00\",\"tool_calls\":null}}]}";
    assert(decode(good) == XG_AI_OK);
    assert(!strcmp(result.answer, "hello \xe4\xbd\xa0\xe5\xa5\xbd \xf0\x9f\x98\x80"));
    assert(!result.truncated);
    snprintf(wire, sizeof(wire), "%s \n\t", good);
    assert(decode(wire) == XG_AI_OK);
    snprintf(wire, sizeof(wire), "%sx", good);
    assert(decode(wire) == XG_AI_INVALID_RESPONSE);
    snprintf(wire, sizeof(wire), "%s%s", good, good);
    assert(decode(wire) == XG_AI_INVALID_RESPONSE);
    const char *bad[] = {
        "", "null", "[]", "{}", "{\"choices\":[]}",
        "{\"choices\":[],\"choices\":[]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":null}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":[]}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\" \"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"user\",\"content\":\"x\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"x\",\"content\":\"y\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"x\\u0000y\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"\\ud800\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"x\n\"}}]}",
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"\xed\xa0\x80\"}}]}",
        "{\"n\":01}", "{\"n\":1.}", "{\"n\":1e}", "{\"n\":+1}", "{\"n\":true,}",
        "{\"n\":NaN}", "{\"n\":Infinity}", "{\"n\":\"\\x00\"}",
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) assert(decode(bad[i]) == XG_AI_INVALID_RESPONSE);
    assert(decode("{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"role\":\"assistant\",\"content\":\"partial\"}}]}") == XG_AI_INCOMPLETE);
    assert(decode("{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"role\":\"assistant\",\"content\":null}}]}") == XG_AI_TOOLS_REJECTED);
    assert(decode("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"ok\",\"tool_calls\":[{\"function\":{\"name\":\"erase\"}}]}}]}") == XG_AI_TOOLS_REJECTED);
    assert(decode("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"ok\",\"function_call\":{}}}]}") == XG_AI_TOOLS_REJECTED);
    char nul[] = "{}\0junk";
    assert(xigua_ai_decode(nul, sizeof(nul)-1, &result) == XG_AI_INVALID_RESPONSE);
    memset(wire, ' ', XG_AI_RESPONSE_MAX + 1);
    assert(xigua_ai_decode(wire, XG_AI_RESPONSE_MAX + 1, &result) == XG_AI_RESPONSE_TOO_LARGE);
    memset(wire, '[', 100); memset(wire + 100, ']', 100); wire[200] = 0;
    assert(decode(wire) == XG_AI_INVALID_RESPONSE);
    strcpy(wire, "[0");
    for (int i = 0; i < 300; ++i) strcat(wire, ",0");
    strcat(wire, "]");
    assert(decode(wire) == XG_AI_INVALID_RESPONSE);
}
static void test_truncation(void)
{
    char text[1501];
    for (size_t i = 0; i < 1500; i += 3) memcpy(text + i, "\xe4\xbd\xa0", 3);
    text[1500] = 0;
    snprintf(wire, sizeof(wire), "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"%s\"}}]}", text);
    assert(decode(wire) == XG_AI_OK);
    assert(result.truncated && strlen(result.answer) == 1023);
    assert(!memcmp(result.answer, text, 1023));
}
int main(void)
{
    test_request(); test_response(); test_truncation();
    puts("xigua AI protocol tests: PASS");
    return 0;
}
