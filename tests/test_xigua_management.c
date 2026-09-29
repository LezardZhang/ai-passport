#include "xigua_management.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static xigua_config_t config, decoded;
static xigua_management_request_t request;
static char wire[XG_CONFIG_WIRE_MAX];
static bool parse(const char *s) { return xigua_management_parse(s, strlen(s), &request); }
int main(void)
{
    xigua_config_defaults(&config);
    assert(xigua_config_valid(&config));
    assert(!xigua_config_ai_valid(&config.ai, true));
    assert(xigua_config_timezone_valid("CST-8"));
    assert(xigua_config_timezone_valid("EST5EDT,M3.2.0/2,M11.1.0/2"));
    assert(xigua_config_timezone_valid("UTC0"));
    assert(!xigua_config_timezone_valid("Asia/Shanghai"));
    assert(!xigua_config_timezone_valid("EST5EDT"));
    assert(!xigua_config_timezone_valid("EST5EDT,M13.2.0,M11.1.0"));
    assert(!xigua_config_timezone_valid("UTC25"));
    assert(!xigua_config_timezone_valid("CST-8\n"));
    assert(parse("{\"id\":1,\"op\":\"status\"}"));
    assert(parse("{\"id\":1,\"op\":\"records.begin\"}"));
    assert(request.op == XG_M_RECORDS_BEGIN);
    assert(parse("{\"id\":1,\"op\":\"records.item\",\"boot_id\":4294967295,\"revision\":4294967295,\"count\":128,\"index\":127}"));
    assert(request.op == XG_M_RECORDS_ITEM && request.boot_id == UINT32_MAX && request.index == 127);
    assert(parse("{\"id\":1,\"op\":\"records.finish\",\"boot_id\":1,\"revision\":0,\"count\":0,\"index\":0}"));
    assert(request.op == XG_M_RECORDS_FINISH);
    assert(!parse("{\"id\":1,\"op\":\"records.item\",\"boot_id\":1,\"revision\":0,\"count\":1,\"index\":1}"));
    assert(!parse("{\"id\":1,\"op\":\"records.finish\",\"boot_id\":1,\"revision\":0,\"count\":2,\"index\":1}"));
    assert(!parse("{\"id\":1,\"op\":\"records.item\",\"boot_id\":4294967296,\"revision\":0,\"count\":1,\"index\":0}"));
    assert(!parse("{\"id\":1,\"op\":\"records.item\",\"boot_id\":1,\"revision\":0,\"count\":129,\"index\":0}"));
    assert(!parse("{\"id\":1,\"op\":\"records.begin\",\"key\":\"secret\"}"));
    assert(!parse("{\"id\":1,\"op\":\"status\",\"key\":\"x\"}"));
    assert(!parse("{\"id\":1,\"id\":2,\"op\":\"status\"}"));
    assert(!parse("{\"id\":1.1,\"op\":\"status\"}"));
    assert(!parse("{\"id\":01,\"op\":\"ai.test\"}"));
    assert(!parse("{\"id\":1.,\"op\":\"ai.test\"}"));
    assert(!parse("{\"id\":1e+,\"op\":\"ai.test\"}"));
    assert(!parse("{\"id\":+1,\"op\":\"ai.test\"}"));
    assert(!parse("{\"id\":1,\"op\":\"ai.ask\",\"text\":\"raw\ttab\"}"));
    assert(!parse("{\"id\":1,\"op\":\"ai.ask\",\"text\":\"raw\nnewline\"}"));
    assert(!parse("{\"id\":1,\"op\":\"ai.ask\",\"text\":\"raw\rcarriage\"}"));
    assert(parse("{\"id\":1,\"op\":\"ai.ask\",\"text\":\"escaped\\t\\n\\r\"}"));
    assert(!parse("{\"id\":2147483648,\"op\":\"status\"}"));
    assert(!parse("{\"id\":1,\"op\":\"status\"}{}"));
    assert(!parse("{\"id\":1,\"op\":\"status\\u0000\"}"));
    assert(!parse("{\"id\":1,\"op\":\"ai.ask\",\"text\":\"\xc0\x80\"}"));
    assert(parse("{\"id\":2,\"op\":\"wifi.set\",\"slot\":7,\"ssid\":\"test-net\",\"password\":\"test-only\",\"enabled\":true,\"priority\":2}"));
    assert(xigua_management_apply(&request, &config));
    request.slot = 6;
    assert(!xigua_management_apply(&request, &config)); /* Duplicate SSID never creates a second identity. */
    assert(!config.wifi[6].ssid[0]);
    assert(!parse("{\"id\":2,\"op\":\"wifi.set\",\"slot\":8,\"ssid\":\"a\",\"password\":\"\",\"enabled\":true,\"priority\":0}"));
    assert(!parse("{\"id\":2,\"op\":\"wifi.set\",\"slot\":0,\"ssid\":\"a\",\"password\":\"short\",\"enabled\":true,\"priority\":0}"));
    assert(parse("{\"id\":3,\"op\":\"ai.set\",\"endpoint\":\"https://example.invalid/v1/chat/completions\",\"key\":\"test-only-key\",\"model\":\"test-model\"}"));
    assert(xigua_management_apply(&request, &config));
    assert(xigua_config_ai_valid(&config.ai, true));
    strcpy(config.ai.endpoint, "http://example.invalid/v1"); assert(!xigua_config_valid(&config));
    strcpy(config.ai.endpoint, "https://user@example.invalid/v1"); assert(!xigua_config_valid(&config));
    strcpy(config.ai.endpoint, "https://example.invalid/v1?key=x"); assert(!xigua_config_valid(&config));
    strcpy(config.ai.endpoint, "https://example.invalid/v1");
    strcpy(config.ai.key, "test-only\r\nX: y"); assert(!xigua_config_valid(&config));
    strcpy(config.ai.key, "test-only-key");
    assert(parse("{\"id\":4,\"op\":\"time.set\",\"timezone\":\"CST-8\"}"));
    assert(xigua_management_apply(&request, &config));
    size_t n = xigua_config_encode(&config, wire, sizeof(wire)); assert(n);
    assert(xigua_config_decode(&decoded, wire, n));
    assert(!strcmp(config.ai.key, decoded.ai.key) && !strcmp(config.ai.endpoint, decoded.ai.endpoint));
    assert(!strcmp(config.wifi[7].ssid, decoded.wifi[7].ssid) && !strcmp(config.timezone, decoded.timezone));
    config = decoded;
    assert(!xigua_config_decode(&decoded, wire, n - 1));
    assert(!memcmp(&config, &decoded, sizeof(config)));
    assert(!xigua_config_encode(&config, wire, 12));
    assert(parse("{\"id\":5,\"op\":\"ai.clear\"}")); assert(xigua_management_apply(&request, &config));
    assert(!config.ai.key[0] && config.wifi[7].enabled);
    assert(parse("{\"id\":6,\"op\":\"wifi.clear\",\"slot\":7}")); assert(xigua_management_apply(&request, &config));
    assert(!config.wifi[7].ssid[0] && !strcmp(config.timezone, "CST-8"));
    assert(parse("{\"id\":7,\"op\":\"ai.ask\",\"text\":\"你好\"}"));
    assert(!xigua_management_apply(&request, &config));
    xigua_config_defaults(&config);
    assert(xigua_management_wifi_upsert(&config, "home", "test-only") == 0);
    assert(xigua_management_wifi_upsert(&config, "guest", "") == 1);
    assert(config.wifi[1].priority == 0 && config.wifi[0].priority == 1);
    assert(xigua_management_wifi_upsert(&config, "home", "new-test-key") == 0);
    assert(config.wifi[0].priority == 0 && config.wifi[1].priority == 1);
    assert(!strcmp(config.wifi[0].password, "new-test-key"));
    decoded = config;
    assert(xigua_management_wifi_upsert(&config, "home", "short") == -1);
    assert(xigua_management_wifi_upsert(&config, "", "test-only") == -1);
    assert(xigua_management_wifi_upsert(&config, "\xc0\x80", "test-only") == -1);
    assert(!memcmp(&config, &decoded, sizeof(config)));
    char max_ssid[34]; memset(max_ssid, 's', 33); max_ssid[33] = 0;
    assert(xigua_management_wifi_upsert(&config, max_ssid, "test-only") == -1);
    max_ssid[32] = 0;
    assert(xigua_management_wifi_upsert(&config, max_ssid, "test-only") == 2);
    char raw_key[65]; memset(raw_key, 'a', 64); raw_key[64] = 0;
    assert(xigua_management_wifi_upsert(&config, "hex", raw_key) == 3);
    raw_key[63] = 'z';
    assert(xigua_management_wifi_upsert(&config, "hex", raw_key) == -1);
    for (unsigned i = 4; i < XG_WIFI_PROFILES; ++i) {
        char ssid[20]; snprintf(ssid, sizeof(ssid), "test-%u", i);
        assert(xigua_management_wifi_upsert(&config, ssid, "test-only") == (int)i);
    }
    decoded = config;
    assert(xigua_management_wifi_upsert(&config, "overflow", "test-only") == -2);
    assert(!memcmp(&config, &decoded, sizeof(config)));
    assert(xigua_management_wifi_upsert(&config, "home", "test-again") == 0);
    assert(config.wifi[0].priority == 0 && xigua_config_valid(&config));
    puts("Xigua configuration and management tests: PASS");
    return 0;
}
