#include "xigua_sound.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int16_t whole[20000], chunks[20000];

static void test_chunk_invariance(void)
{
    static const size_t sizes[] = {1, 319, 7, 641, 320, 1024, 3};
    for (unsigned track = 0; track < XIGUA_SOUND_TRACK_COUNT; ++track) {
        xigua_sound_t a, b;
        assert(xigua_sound_init(&a, (xigua_sound_track_t)track));
        assert(xigua_sound_init(&b, (xigua_sound_track_t)track));
        size_t expected = track == XIGUA_SOUND_ALERT ? XIGUA_SOUND_ALERT_SAMPLES : 20000;
        assert(xigua_sound_render(&a, whole, 20000) == expected);
        size_t offset = 0, generated = 0, call = 0;
        while (offset < 20000) {
            size_t count = sizes[call++ % (sizeof(sizes) / sizeof(sizes[0]))];
            if (count > 20000 - offset) count = 20000 - offset;
            generated += xigua_sound_render(&b, chunks + offset, count);
            offset += count;
        }
        assert(generated == expected);
        assert(memcmp(whole, chunks, sizeof(whole)) == 0);
        assert(xigua_sound_init(&b, (xigua_sound_track_t)track));
        assert(xigua_sound_render(&b, chunks, 20000) == expected);
        assert(memcmp(whole, chunks, sizeof(whole)) == 0);
        for (size_t i = 0; i < 20000; ++i) {
            assert(whole[i] <= XIGUA_SOUND_PEAK_LIMIT);
            assert(whole[i] >= -XIGUA_SOUND_PEAK_LIMIT);
        }
        assert(whole[0] == 0);
    }
}

static void test_invalid_and_noop(void)
{
    xigua_sound_t state, saved;
    assert(!xigua_sound_init(NULL, XIGUA_SOUND_WHITE));
    for (int track = -1; track <= XIGUA_SOUND_TRACK_COUNT; track += XIGUA_SOUND_TRACK_COUNT + 1) {
        assert(!xigua_sound_init(&state, (xigua_sound_track_t)track));
        memset(chunks, 0x55, sizeof(chunks));
        assert(xigua_sound_render(&state, chunks, 20) == 0);
        for (size_t i = 0; i < 20; ++i) assert(chunks[i] == 0);
        assert(chunks[20] == 0x5555);
    }
    memset(chunks, 0x55, sizeof(chunks));
    assert(xigua_sound_render(NULL, chunks, 20) == 0);
    for (size_t i = 0; i < 20; ++i) assert(chunks[i] == 0);
    assert(xigua_sound_init(&state, XIGUA_SOUND_RAIN));
    saved = state;
    assert(xigua_sound_render(&state, NULL, 20) == 0);
    assert(xigua_sound_render(&state, chunks, 0) == 0);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    assert(sizeof(state) <= 64);
}

static void test_wave_wrap_chunks(void)
{
    xigua_sound_t a, b;
    assert(xigua_sound_init(&a, XIGUA_SOUND_WAVES));
    assert(xigua_sound_init(&b, XIGUA_SOUND_WAVES));
    /* Use deliberately unequal call boundaries across several envelope wraps.
     * An internal phase reset must never reset noise, filters, or fade-in. */
    for (unsigned block = 0; block < 25; ++block) {
        assert(xigua_sound_render(&a, whole, 20000) == 20000);
        size_t offset = 0;
        while (offset < 20000) {
            size_t count = 1u + ((offset * 17u + block) % 997u);
            if (count > 20000 - offset) count = 20000 - offset;
            assert(xigua_sound_render(&b, chunks + offset, count) == count);
            offset += count;
        }
        assert(memcmp(whole, chunks, sizeof(whole)) == 0);
    }
}

static void test_alert(void)
{
    xigua_sound_t state;
    assert(xigua_sound_init(&state, XIGUA_SOUND_ALERT));
    assert(xigua_sound_render(&state, whole, 20000) == XIGUA_SOUND_ALERT_SAMPLES);
    unsigned groups = 0, rising[3] = {0};
    bool active = false;
    /* Gaps longer than 100 ms distinguish intentional tones from oscillator
     * zero crossings. Verify three audible groups and rising pitch by signal. */
    unsigned silence = 2000;
    int64_t sum = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        sum += whole[i];
        if (whole[i] == 0) {
            ++silence;
            if (silence >= 1600) active = false;
        } else {
            if (!active) { ++groups; active = true; }
            silence = 0;
            assert(groups <= 3);
            if (i && whole[i] > 0 && whole[i - 1] <= 0) ++rising[groups - 1];
        }
        if (i >= XIGUA_SOUND_ALERT_SAMPLES) assert(whole[i] == 0);
    }
    assert(groups == 3);
    assert(rising[0] > 90 && rising[0] < 140);
    assert(rising[1] > rising[0] && rising[2] > rising[1]);
    assert(sum > -16000 && sum < 16000);
    assert(xigua_sound_render(&state, chunks, 320) == 0);
    for (unsigned i = 0; i < 320; ++i) assert(chunks[i] == 0);
    /* Last nonzero frame has a ramp down; end cannot leave a held DC value. */
    assert(whole[2879] == 0 && whole[7679] == 0 && whole[12479] == 0);
}

static void test_continuous_profiles(void)
{
    double mean_square[3], roughness[3], pulse_ratio[3];
    for (unsigned track = 0; track < 3; ++track) {
        xigua_sound_t state;
        assert(xigua_sound_init(&state, (xigua_sound_track_t)track));
        int64_t sum = 0, square = 0, difference = 0;
        int16_t previous = 0;
        int64_t quiet = 0, loud = 0;
        unsigned clipped = 0;
        int64_t smallest_chunk = INT64_MAX, largest_chunk = 0;
        /* Two minutes exercise 15 swell wraps, filter steady state and several
         * million samples without a precomputed looping PCM buffer. */
        const unsigned total = 120u * XIGUA_SOUND_SAMPLE_RATE;
        for (unsigned offset = 0; offset < total; offset += 320) {
            assert(xigua_sound_render(&state, chunks, 320) == 320);
            int64_t chunk_energy = 0;
            for (unsigned i = 0; i < 320; ++i) {
                int32_t value = chunks[i];
                assert(value >= -XIGUA_SOUND_PEAK_LIMIT && value <= XIGUA_SOUND_PEAK_LIMIT);
                if (value == XIGUA_SOUND_PEAK_LIMIT || value == -XIGUA_SOUND_PEAK_LIMIT) ++clipped;
                sum += value;
                square += (int64_t)value * value;
                chunk_energy += (int64_t)value * value;
                int32_t delta = value - previous;
                difference += (int64_t)delta * delta;
                previous = (int16_t)value;
                unsigned position = (offset + i) % (8u * XIGUA_SOUND_SAMPLE_RATE);
                if (position < XIGUA_SOUND_SAMPLE_RATE) quiet += (int64_t)value * value;
                if (position >= 3u * XIGUA_SOUND_SAMPLE_RATE &&
                    position < 4u * XIGUA_SOUND_SAMPLE_RATE) loud += (int64_t)value * value;
            }
            if (offset > XIGUA_SOUND_SAMPLE_RATE) {
                if (chunk_energy < smallest_chunk) smallest_chunk = chunk_energy;
                if (chunk_energy > largest_chunk) largest_chunk = chunk_energy;
            }
        }
        pulse_ratio[track] = (double)largest_chunk / smallest_chunk;
        double mean = (double)sum / total;
        mean_square[track] = (double)square / total;
        roughness[track] = (double)difference / square;
        assert(mean > -15.0 && mean < 15.0);
        assert(mean_square[track] > 1000000.0 && mean_square[track] < 60000000.0);
        assert(clipped < total / 1000); /* Saturation must be exceptionally rare. */
        if (track == XIGUA_SOUND_WAVES) assert(loud > quiet * 4);
        printf("sound %u: mean %.3f, mean-square %.0f, roughness %.4f, clipped %u, pulse %.2f\n",
               track, mean, mean_square[track], roughness[track], clipped, pulse_ratio[track]);
    }
    /* Adjacent-sample energy is a spectrum proxy: white is uncorrelated, rain
     * filtered, waves distinctly smoother. These check audible properties. */
    assert(roughness[0] > 1.8 && roughness[0] < 2.2);
    assert(roughness[1] < roughness[0] / 2);
    assert(roughness[2] < roughness[1]);
    /* A stationary noise bed cannot stand in for rain: short windows must
     * contain distinct splashes. Ocean has a much deeper multi-second swell. */
    assert(pulse_ratio[0] < 2.0);
    assert(pulse_ratio[1] > 3.0);
    assert(pulse_ratio[2] > 50.0);
    assert(mean_square[0] < mean_square[1] * 4);
    assert(mean_square[0] < mean_square[2] * 4);
}

int main(void)
{
    test_chunk_invariance();
    test_invalid_and_noop();
    test_wave_wrap_chunks();
    test_alert();
    test_continuous_profiles();
    printf("xigua sound tests passed; state bytes %zu\n", sizeof(xigua_sound_t));
    return 0;
}
