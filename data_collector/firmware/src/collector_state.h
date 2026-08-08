#ifndef COLLECTOR_STATE_H
#define COLLECTOR_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum e_collector_state
{
    COLLECTOR_STATE_INITIALIZING,
    COLLECTOR_STATE_WAITING,
    COLLECTOR_STATE_RECORDING,
    COLLECTOR_STATE_STOPPING,
    COLLECTOR_STATE_ERROR
} collector_state_t;

typedef struct st_collector_statistics
{
    uint32_t captured_frames;
    uint32_t encoded_frames;
    uint32_t transmitted_frames;
    uint32_t dropped_frames;
    uint32_t feed_markers;
} collector_statistics_t;

typedef struct st_collector_context
{
    collector_state_t      state;
    collector_statistics_t statistics;
    uint32_t               session_id;
    uint32_t               session_start_ms;
    uint32_t               next_sequence;
    bool                   usb_ready;
} collector_context_t;

void collector_state_initialize(collector_context_t * p_context);
void collector_state_set_usb_ready(collector_context_t * p_context, bool ready);
bool collector_state_start(collector_context_t * p_context, uint32_t now_ms, uint32_t session_id);
bool collector_state_stop(collector_context_t * p_context);
void collector_state_stopped(collector_context_t * p_context);
bool collector_state_feed_marker(collector_context_t * p_context);
uint32_t collector_state_timestamp(const collector_context_t * p_context, uint32_t now_ms);
uint32_t collector_state_next_sequence(collector_context_t * p_context);

#endif