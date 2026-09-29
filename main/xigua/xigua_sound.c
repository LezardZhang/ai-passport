#include "xigua_sound.h"

#include <string.h>

#define FADE_SAMPLES 320u
#define WAVE_PERIOD (8u * XIGUA_SOUND_SAMPLE_RATE)
#define TONE_SLOT 4800u
#define TONE_LENGTH 2880u

/* One oscillator cycle; linear interpolation keeps the small table smooth. */
static const int16_t sine[33] = {
    0, 1171, 2296, 3333, 4243, 4989, 5543, 5885,
    6000, 5885, 5543, 4989, 4243, 3333, 2296, 1171,
    0, -1171, -2296, -3333, -4243, -4989, -5543, -5885,
    -6000, -5885, -5543, -4989, -4243, -3333, -2296, -1171, 0
};

bool xigua_sound_init(xigua_sound_t *state, xigua_sound_track_t track)
{
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    state->track = XIGUA_SOUND_TRACK_COUNT;
    if ((unsigned)track >= XIGUA_SOUND_TRACK_COUNT) return false;
    state->track = track;
    state->random = UINT32_C(0x6d2b79f5);
    return true;
}

static int32_t noise(xigua_sound_t *state)
{
    uint32_t value = state->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state->random = value;
    /* Symmetric integer distribution, avoiding implementation-defined casts. */
    return ((int32_t)(value >> 16) * 2 - 65535) / 16;
}

/* Table amplitude is 6000; every caller retains headroom in 16-bit PCM. */
static int32_t oscillator(uint32_t phase)
{
    uint32_t index = phase >> 11;
    uint32_t fraction = phase & 2047u;
    return sine[index] +
        (sine[index + 1] - sine[index]) * (int32_t)fraction / 2048;
}

static int32_t rain_drop(xigua_sound_t *state, int32_t noise_value)
{
    if (!state->drop_wait && state->drop_age >= state->drop_length) {
        /* Irregular, short resonant splashes, separated by 20..120 ms. The
         * attack and decay are continuous; these are not hard PCM clicks. */
        state->drop_age = 0;
        state->drop_length = (uint16_t)(640u + (state->random & 1023u));
        state->drop_wait = (uint16_t)(320u + ((state->random >> 10) % 1600u));
        state->drop_step = (uint16_t)((1000u + (state->random >> 21) % 1400u) *
                                      65536u / XIGUA_SOUND_SAMPLE_RATE);
        state->phase = 0;
    }
    if (state->drop_age >= state->drop_length) {
        if (state->drop_wait) --state->drop_wait;
        return 0;
    }
    uint32_t age = state->drop_age++;
    int32_t remaining = (int32_t)(state->drop_length - age);
    int32_t envelope = remaining * 1024 / state->drop_length;
    envelope = envelope * envelope / 1024;
    if (age < 80u) envelope = envelope * (int32_t)age / 80;
    /* A little broadband spray keeps drops textured rather than musical. */
    int32_t value = oscillator(state->phase) * 2 + noise_value / 2;
    state->phase = (state->phase + state->drop_step) & 65535u;
    return value * envelope / 1024;
}

static int32_t ambient(xigua_sound_t *state)
{
    int32_t raw = noise(state);
    int32_t value = raw * 2; /* White: steady, bright, broadband hiss. */
    if (state->track != XIGUA_SOUND_WHITE) {
        /* Q8 state avoids DC bias. Rain has a soft bed plus discrete patter;
         * waves have a low roar and audible midrange surf, with deep swells. */
        int32_t divisor = state->track == XIGUA_SOUND_RAIN ? 4 : 12;
        state->lowpass += (raw * 256 - state->lowpass) / divisor;
        state->dc += (state->lowpass - state->dc) / 1024;
        value = (state->lowpass - state->dc) / 128;
        if (state->track == XIGUA_SOUND_RAIN) {
            value += rain_drop(state, raw);
        } else {
            uint32_t phase = state->position;
            uint32_t ramp = phase < WAVE_PERIOD / 2 ? phase : WAVE_PERIOD - phase;
            int32_t t = (int32_t)(ramp * 1024 / (WAVE_PERIOD / 2));
            int32_t smooth = t * t / 1024;
            smooth = smooth * (3072 - 2 * t) / 1024;
            /* Strong 8-second quiet/crest rhythm survives a small speaker's
             * weak bass response; surf adds midrange only near the crest. */
            value = value * 4 * (64 + 15 * smooth / 16) / 1024;
            value += raw * smooth / 2048;
            if (++state->position == WAVE_PERIOD) state->position = 0;
        }
    }
    if (value > XIGUA_SOUND_PEAK_LIMIT) value = XIGUA_SOUND_PEAK_LIMIT;
    if (value < -XIGUA_SOUND_PEAK_LIMIT) value = -XIGUA_SOUND_PEAK_LIMIT;
    if (state->fade < FADE_SAMPLES) {
        value = value * state->fade / (int32_t)FADE_SAMPLES;
        ++state->fade;
    }
    return value;
}

static int32_t alert(xigua_sound_t *state)
{
    uint32_t position = state->position++;
    uint32_t tone = position / TONE_SLOT;
    uint32_t within = position % TONE_SLOT;
    if (tone >= 3 || within >= TONE_LENGTH) return 0;
    if (within == 0) state->phase = 0;
    /* 16-bit phase, frequencies 660, 880 and 1100 Hz at the fixed PCM rate. */
    uint32_t frequency = 660u + 220u * tone;
    int32_t value = oscillator(state->phase) * 2;
    state->phase = (state->phase + frequency * 65536u / XIGUA_SOUND_SAMPLE_RATE) & 65535u;
    uint32_t envelope = FADE_SAMPLES;
    if (within < envelope) envelope = within;
    if (TONE_LENGTH - 1 - within < envelope) envelope = TONE_LENGTH - 1 - within;
    return value * (int32_t)envelope / (int32_t)FADE_SAMPLES;
}

size_t xigua_sound_render(xigua_sound_t *state, int16_t *output, size_t samples)
{
    if (!output || !samples) return 0;
    size_t generated = 0;
    if (state && (unsigned)state->track < XIGUA_SOUND_TRACK_COUNT) {
        while (generated < samples) {
            if (state->track == XIGUA_SOUND_ALERT &&
                state->position >= XIGUA_SOUND_ALERT_SAMPLES) break;
            output[generated++] = (int16_t)(state->track == XIGUA_SOUND_ALERT ?
                alert(state) : ambient(state));
        }
    }
    for (size_t i = generated; i < samples; ++i) output[i] = 0;
    return generated;
}
