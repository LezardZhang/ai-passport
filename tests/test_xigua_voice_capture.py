#!/usr/bin/env python3
"""Run the actual capture functions against NOR Flash and microphone doubles."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def capture_source(source: str) -> str:
    definitions = []
    lines = iter(source.splitlines(keepends=True))
    for line in lines:
        if re.match(r"#define XIGUA_AI_(VOICE|WAV|AUDIO)_", line):
            definition = line
            while line.rstrip().endswith("\\"):
                line = next(lines)
                definition += line
            definitions.append(definition)
    for name in ("wav_put_u16", "wav_put_u32", "wav_header", "record_voice"):
        match = re.search(
            rf"^static [^\n]+\b{name}\([^;]*?\)\n\{{.*?^\}}",
            source, re.MULTILINE | re.DOTALL,
        )
        if not match:
            raise AssertionError(f"capture function missing: {name}")
        definitions.append(match.group(0) + "\n")
    return "\n".join(definitions)


STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_STATE 3
#define MALLOC_CAP_8BIT 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct { size_t size; } esp_partition_t;
static esp_partition_t partition = { 2 * 1024 * 1024 };
static uint8_t flash[2 * 1024 * 1024];
static volatile bool s_voice_stop;
static size_t header_writes, read_calls, fail_read_at;
static const esp_partition_t *voice_partition(void) { return &partition; }
static esp_err_t esp_partition_erase_range(const esp_partition_t *p,
                                          size_t offset, size_t length) {
    assert(p == &partition && offset + length <= p->size);
    memset(flash + offset, 0xff, length);
    return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *p, size_t offset,
                                    const void *data, size_t length) {
    assert(p == &partition && offset + length <= p->size);
    if (!offset) header_writes++;
    const uint8_t *bytes = data;
    /* NOR Flash programming can only change an erased 1 bit into 0. */
    for (size_t i = 0; i < length; i++) flash[offset+i] &= bytes[i];
    return ESP_OK;
}
static esp_err_t bsp_audio_init(void) { return ESP_OK; }
static esp_err_t bsp_audio_set_format(unsigned hz, unsigned bits, unsigned ch) {
    assert(hz == 16000 && bits == 16 && ch == 1); return ESP_OK;
}
static esp_err_t bsp_audio_wake(void) { return ESP_OK; }
static esp_err_t bsp_audio_sleep(void) { return ESP_OK; }
static esp_err_t bsp_audio_read(void *data, size_t bytes) {
    if (read_calls++ == fail_read_at) return ESP_FAIL;
    memset(data, 0x5a, bytes); return ESP_OK;
}
'''

CHECKS = r'''
static uint32_t u32(size_t offset) {
    return (uint32_t)flash[offset] | (uint32_t)flash[offset+1] << 8 |
           (uint32_t)flash[offset+2] << 16 | (uint32_t)flash[offset+3] << 24;
}
static void setup(bool stop, size_t failure) {
    s_voice_stop = stop; fail_read_at = failure;
    header_writes = read_calls = 0;
}
static void check_wav(size_t length) {
    assert(header_writes == 1);
    assert(!memcmp(flash, "RIFF", 4) && !memcmp(flash+8, "WAVEfmt ", 8));
    assert(!memcmp(flash+36, "data", 4));
    assert(u32(4) == length-8 && u32(40) == length-44);
    assert(u32(16) == 16 && flash[20] == 1 && flash[22] == 1);
    assert(u32(24) == 16000 && u32(28) == 32000);
    assert(flash[32] == 2 && flash[34] == 16);
    for (size_t i = 44; i < length; i++) assert(flash[i] == 0x5a);
}
int main(void) {
    size_t length = 0;
    setup(true, SIZE_MAX);
    assert(record_voice(&length) == ESP_OK);
    assert(length >= 32000+44 && length <= 32000+2048+44);
    check_wav(length);
    setup(false, SIZE_MAX);
    assert(record_voice(&length) == ESP_OK && length == 60*32000+44);
    check_wav(length);
    setup(false, 1);
    assert(record_voice(&length) == ESP_FAIL && header_writes == 0);
    assert(flash[0] == 0xff && flash[43] == 0xff);
    assert(record_voice(NULL) == ESP_ERR_INVALID_ARG);
    puts("Xigua voice capture NOR Flash/WAV tests: PASS");
    return 0;
}
'''


def main() -> None:
    source = (ROOT / "main/xigua_ai.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="xigua-voice-test-") as directory:
        path = Path(directory)
        test = path / "capture.c"
        exe = path / ("capture.exe" if os.name == "nt" else "capture")
        test.write_text(STUBS + capture_source(source) + CHECKS, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                  str(test), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
