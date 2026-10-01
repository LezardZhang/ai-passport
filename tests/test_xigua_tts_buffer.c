#include "xigua_tts_buffer.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    xigua_tts_buffer_t state = {0};
    assert(XIGUA_TTS_PREFILL_BYTES == 72000); /* Exactly 3 s of 12 kHz PCM16. */
    assert(!xigua_tts_buffer_ready(&state, 71998, false));
    assert(xigua_tts_buffer_ready(&state, 72000, false));
    assert(xigua_tts_buffer_ready(&state, 1024, false));
    assert(!xigua_tts_buffer_ready(&state, 0, false) && state.underruns == 1);
    assert(!xigua_tts_buffer_ready(&state, 0, false) && state.underruns == 1);
    assert(!xigua_tts_buffer_ready(&state, 71998, false));
    assert(xigua_tts_buffer_ready(&state, 72000, false));
    state = (xigua_tts_buffer_t){0};
    assert(xigua_tts_buffer_ready(&state, 2, true)); /* Short completed speech. */
    assert(!xigua_tts_buffer_ready(&state, 0, true) && !state.underruns);
    /* A two-second producer stall is covered by the three-second reserve. */
    state = (xigua_tts_buffer_t){0};
    size_t queued = 72000;
    for (int i = 0; i < 100; ++i) {
        assert(xigua_tts_buffer_ready(&state, queued, false));
        queued -= 480; /* 20 ms. */
    }
    assert(queued == 24000 && !state.underruns);
    puts("Three-second prefill, jitter reserve, short tails and rebuffer policy: PASS");
}
