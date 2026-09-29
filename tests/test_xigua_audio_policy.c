#include "xigua_audio_policy.h"
#include <assert.h>
#include <stdio.h>

static xigua_audio_selection_t next(const xigua_audio_policy_t *p)
{
    return xigua_audio_policy_select(p, 320);
}

int main(void)
{
    xigua_audio_policy_t p;
    xigua_audio_policy_init(&p);
    assert(p.volume == XIGUA_AUDIO_VOLUME_DEFAULT);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);
    assert(!xigua_audio_policy_play(&p, 3));
    assert(xigua_audio_policy_play(&p, 0));
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_BACKGROUND);
    assert(next(&p).track == 0);

    xigua_audio_policy_alert(&p);
    xigua_audio_selection_t alert = next(&p);
    assert(alert.segment == XIGUA_AUDIO_SEGMENT_ALERT);
    assert(alert.samples == 320);
    xigua_audio_policy_rendered(&p, &alert, alert.samples);
    assert(p.alert_samples_remaining == XIGUA_AUDIO_ALERT_SAMPLES - 320);
    for (unsigned i = 1; i < 50; ++i) {
        alert = next(&p);
        assert(alert.segment == XIGUA_AUDIO_SEGMENT_ALERT);
        xigua_audio_policy_rendered(&p, &alert, alert.samples);
    }
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_BACKGROUND);
    assert(next(&p).track == 0); /* Alert resumes only current desired track. */

    xigua_audio_policy_alert(&p);
    alert = next(&p);
    assert(xigua_audio_policy_play(&p, 2));
    assert(xigua_audio_policy_is_current(&p, &alert));
    xigua_audio_policy_rendered(&p, &alert, 320);
    assert(p.alert_samples_remaining == XIGUA_AUDIO_ALERT_SAMPLES - 320);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_ALERT);
    xigua_audio_policy_dismiss_alert(&p);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_BACKGROUND);
    assert(next(&p).track == 2);

    xigua_audio_policy_alert(&p);
    xigua_audio_policy_stop(&p);
    assert(!xigua_audio_policy_is_current(&p, &alert));
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);
    assert(p.alert_samples_remaining == 0);
    xigua_audio_policy_alert(&p); /* Timer can alert even without BGM. */
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_ALERT);
    xigua_audio_policy_pause(&p);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);

    /* Latest intent wins even if the worker has an old selection in flight. */
    assert(xigua_audio_policy_play(&p, 0));
    xigua_audio_selection_t stale = next(&p);
    xigua_audio_policy_stop(&p);
    assert(xigua_audio_policy_play(&p, 1));
    assert(!xigua_audio_policy_is_current(&p, &stale));
    xigua_audio_policy_fail_if_current(&p, &stale);
    assert(!p.fault && next(&p).track == 1);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_BACKGROUND);

    xigua_audio_selection_t current = next(&p);
    xigua_audio_policy_fail_if_current(&p, &current);
    assert(p.fault && next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);
    assert(p.desired == XIGUA_AUDIO_DESIRED_PAUSE);
    xigua_audio_policy_volume(&p, 39);
    assert(p.volume == 35 && next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);
    xigua_audio_policy_volume(&p, 255);
    assert(p.volume == 100);
    assert(xigua_audio_policy_play(&p, 1));
    assert(!p.fault && next(&p).segment == XIGUA_AUDIO_SEGMENT_BACKGROUND);

    /* A dismissed/replaced alert cannot consume the new alert's budget. */
    xigua_audio_policy_alert(&p);
    stale = next(&p);
    xigua_audio_policy_alert(&p);
    xigua_audio_policy_rendered(&p, &stale, 320);
    assert(p.alert_samples_remaining == XIGUA_AUDIO_ALERT_SAMPLES);
    xigua_audio_policy_fail_if_current(&p, &stale);
    assert(!p.fault);

    /* A changed BGM track does not mask a failure of the same active alert. */
    current = next(&p);
    assert(current.segment == XIGUA_AUDIO_SEGMENT_ALERT);
    assert(xigua_audio_policy_play(&p, 2));
    xigua_audio_policy_fail_if_current(&p, &current);
    assert(p.fault && next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);
    assert(xigua_audio_policy_play(&p, 1));

    current = next(&p);
    xigua_audio_policy_fail_if_current(&p, &current);
    assert(p.fault && p.desired == XIGUA_AUDIO_DESIRED_PAUSE);
    xigua_audio_policy_alert(&p);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_ALERT);
    xigua_audio_policy_dismiss_alert(&p);
    assert(next(&p).segment == XIGUA_AUDIO_SEGMENT_IDLE);

    puts("xigua audio policy PASS");
    return 0;
}
