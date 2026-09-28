#include "collector_state.h"

#include <string.h>

void collector_state_initialize(collector_context_t * p_context)
{
    memset(p_context, 0, sizeof(*p_context));
    p_context->state = COLLECTOR_STATE_WAITING;
}

void collector_state_set_usb_ready(collector_context_t * p_context, bool ready)
{
    p_context->usb_ready = ready;
    if (!ready && (p_context->state == COLLECTOR_STATE_RECORDING))
    {
        p_context->state = COLLECTOR_STATE_STOPPING;
    }
}

bool collector_state_start(collector_context_t * p_context, uint32_t now_ms, uint32_t session_id)
{
    if ((p_context->state != COLLECTOR_STATE_WAITING) || !p_context->usb_ready)
    {
        return false;
    }

    memset(&p_context->statistics, 0, sizeof(p_context->statistics));
    p_context->session_id       = session_id;
    p_context->session_start_ms = now_ms;
    p_context->next_sequence    = 0U;
    p_context->state            = COLLECTOR_STATE_RECORDING;
    return true;
}

bool collector_state_stop(collector_context_t * p_context)
{
    if (p_context->state != COLLECTOR_STATE_RECORDING)
    {
        return false;
    }

    p_context->state = COLLECTOR_STATE_STOPPING;
    return true;
}

void collector_state_stopped(collector_context_t * p_context)
{
    if (p_context->state == COLLECTOR_STATE_STOPPING)
    {
        p_context->state = COLLECTOR_STATE_WAITING;
    }
}

bool collector_state_feed_marker(collector_context_t * p_context)
{
    if (p_context->state != COLLECTOR_STATE_RECORDING)
    {
        return false;
    }

    p_context->statistics.feed_markers++;
    return true;
}

uint32_t collector_state_timestamp(const collector_context_t * p_context, uint32_t now_ms)
{
    return now_ms - p_context->session_start_ms;
}

uint32_t collector_state_next_sequence(collector_context_t * p_context)
{
    uint32_t sequence = p_context->next_sequence;
    p_context->next_sequence++;
    return sequence;
}