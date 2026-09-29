#include "xigua_audio_policy.h"

void xigua_audio_policy_init(xigua_audio_policy_t *policy)
{
    if (!policy) return;
    *policy = (xigua_audio_policy_t){.desired = XIGUA_AUDIO_DESIRED_STOP,
                                     .volume = XIGUA_AUDIO_VOLUME_DEFAULT};
}

bool xigua_audio_policy_play(xigua_audio_policy_t *policy, uint8_t track)
{
    if (!policy || track >= XIGUA_AUDIO_TRACK_COUNT) return false;
    policy->desired = XIGUA_AUDIO_DESIRED_PLAY;
    policy->track = track;
    policy->generation++;
    policy->fault = false; /* A fresh user request is the only background retry. */
    return true;
}

void xigua_audio_policy_pause(xigua_audio_policy_t *policy)
{
    if (!policy) return;
    policy->desired = XIGUA_AUDIO_DESIRED_PAUSE;
    policy->alert_samples_remaining = 0;
    policy->generation++;
}

void xigua_audio_policy_stop(xigua_audio_policy_t *policy)
{
    if (!policy) return;
    policy->desired = XIGUA_AUDIO_DESIRED_STOP;
    policy->alert_samples_remaining = 0;
    policy->generation++;
}

void xigua_audio_policy_alert(xigua_audio_policy_t *policy)
{
    if (!policy) return;
    policy->alert_serial++;
    policy->alert_samples_remaining = XIGUA_AUDIO_ALERT_SAMPLES;
    policy->fault = false;
}

void xigua_audio_policy_dismiss_alert(xigua_audio_policy_t *policy)
{
    if (!policy) return;
    policy->alert_samples_remaining = 0;
    policy->alert_serial++;
}

void xigua_audio_policy_volume(xigua_audio_policy_t *policy, uint8_t volume)
{
    if (!policy) return;
    if (volume > XIGUA_AUDIO_VOLUME_MAX) volume = XIGUA_AUDIO_VOLUME_MAX;
    policy->volume = (uint8_t)(volume - volume % XIGUA_AUDIO_VOLUME_STEP);
}

xigua_audio_selection_t xigua_audio_policy_select(const xigua_audio_policy_t *policy,
                                                   size_t chunk_samples)
{
    xigua_audio_selection_t selection = {0};
    if (!policy) return selection;
    selection.track = policy->track;
    selection.volume = policy->volume;
    selection.generation = policy->generation;
    selection.alert_serial = policy->alert_serial;
    if (policy->fault || !chunk_samples) return selection;
    if (policy->alert_samples_remaining) {
        selection.segment = XIGUA_AUDIO_SEGMENT_ALERT;
        selection.samples = policy->alert_samples_remaining < chunk_samples
                          ? policy->alert_samples_remaining : chunk_samples;
    } else if (policy->desired == XIGUA_AUDIO_DESIRED_PLAY) {
        selection.segment = XIGUA_AUDIO_SEGMENT_BACKGROUND;
        selection.samples = chunk_samples;
    }
    return selection;
}

bool xigua_audio_policy_is_current(const xigua_audio_policy_t *policy,
                                   const xigua_audio_selection_t *selection)
{
    if (!policy || !selection || policy->fault) return false;
    if (selection->segment == XIGUA_AUDIO_SEGMENT_ALERT)
        return selection->alert_serial == policy->alert_serial &&
               policy->alert_samples_remaining > 0;
    if (selection->segment == XIGUA_AUDIO_SEGMENT_BACKGROUND)
        return selection->generation == policy->generation &&
               selection->track == policy->track &&
               policy->desired == XIGUA_AUDIO_DESIRED_PLAY &&
               policy->alert_samples_remaining == 0;
    return false;
}

void xigua_audio_policy_rendered(xigua_audio_policy_t *policy,
                                 const xigua_audio_selection_t *selection,
                                 size_t samples)
{
    if (!policy || !selection || selection->segment != XIGUA_AUDIO_SEGMENT_ALERT ||
        selection->alert_serial != policy->alert_serial ||
        !policy->alert_samples_remaining) return;
    if (samples >= policy->alert_samples_remaining) policy->alert_samples_remaining = 0;
    else policy->alert_samples_remaining -= samples;
}

void xigua_audio_policy_fail_if_current(xigua_audio_policy_t *policy,
                                        const xigua_audio_selection_t *selection)
{
    if (!policy || !selection || selection->segment == XIGUA_AUDIO_SEGMENT_IDLE) return;
    if (xigua_audio_policy_is_current(policy, selection)) {
        policy->fault = true;
        /* Never let a later timer alert revive a failed background stream. */
        policy->desired = XIGUA_AUDIO_DESIRED_PAUSE;
        policy->alert_samples_remaining = 0;
    }
}
