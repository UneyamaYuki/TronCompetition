#include "collector_app.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "collector_platform.h"
#include "collector_protocol.h"
#include "collector_state.h"

#include <stdio.h>
#include <string.h>

#define COLLECTOR_FRAME_WIDTH          (640U)
#define COLLECTOR_FRAME_HEIGHT         (480U)
#define COLLECTOR_BYTES_PER_PIXEL      (2U)
#define COLLECTOR_CAPTURE_STRIDE       (1024U * COLLECTOR_BYTES_PER_PIXEL)
#define COLLECTOR_FRAME_ROW_BYTES      (COLLECTOR_FRAME_WIDTH * COLLECTOR_BYTES_PER_PIXEL)
#define COLLECTOR_FRAME_BYTES          (COLLECTOR_FRAME_ROW_BYTES * COLLECTOR_FRAME_HEIGHT)
#define COLLECTOR_EVENT_QUEUE_LENGTH   (8U)
#define COLLECTOR_TASK_STACK_WORDS     (2048U)
#define COLLECTOR_TASK_PRIORITY        (configMAX_PRIORITIES - 3U)
#define COLLECTOR_SAMPLE_PERIOD_MS     (200U)
#define COLLECTOR_DEBOUNCE_MS          (200U)

typedef enum e_collector_event_type
{
    COLLECTOR_EVENT_FRAME,
    COLLECTOR_EVENT_TOGGLE,
    COLLECTOR_EVENT_FEED_MARKER,
    COLLECTOR_EVENT_USB_CONTROL
} collector_event_type_t;

typedef struct st_collector_event
{
    collector_event_type_t type;
    void                  * p_frame;
    TickType_t              tick;
} collector_event_t;

static QueueHandle_t       s_event_queue;
static collector_context_t s_context;
static TickType_t          s_last_sample_tick;
static TickType_t          s_last_toggle_tick;
static TickType_t          s_last_marker_tick;
static volatile bool       s_frame_event_pending;
static uint8_t             s_frame_buffer[COLLECTOR_FRAME_BYTES]
    BSP_PLACE_IN_SECTION(".sdram_noinit_nocache") BSP_ALIGN_VARIABLE(64);
static uint8_t             s_raw_buffer[COLLECTOR_FRAME_BYTES + sizeof(collector_frame_metadata_t)]
    BSP_PLACE_IN_SECTION(".sdram_noinit_nocache") BSP_ALIGN_VARIABLE(64);

static void collector_task(void * p_context);
static bool record_write(collector_record_type_t type, uint32_t timestamp_ms,
                         const void * p_payload, uint32_t payload_size);
static void session_start(TickType_t tick);
static void session_stop(TickType_t tick);
static void frame_process(const collector_event_t * p_event);
static void event_from_isr(collector_event_type_t type, void * p_frame);
static uint32_t tick_to_ms(TickType_t tick);
static void error_write(uint32_t timestamp_ms, const char * p_code);

bool collector_app_start(void)
{
    collector_state_initialize(&s_context);
    s_event_queue = xQueueCreate(COLLECTOR_EVENT_QUEUE_LENGTH, sizeof(collector_event_t));
    if (s_event_queue == NULL)
    {
        return false;
    }

    if (xTaskCreate(collector_task, "collector", COLLECTOR_TASK_STACK_WORDS, NULL,
                    COLLECTOR_TASK_PRIORITY, NULL) != pdPASS)
    {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return false;
    }

    return true;
}

void collector_app_frame_from_isr(void * p_frame)
{
    event_from_isr(COLLECTOR_EVENT_FRAME, p_frame);
}

void collector_app_toggle_from_isr(void)
{
    event_from_isr(COLLECTOR_EVENT_TOGGLE, NULL);
}

void collector_app_feed_marker_from_isr(void)
{
    event_from_isr(COLLECTOR_EVENT_FEED_MARKER, NULL);
}

void collector_app_usb_control_from_isr(void)
{
    event_from_isr(COLLECTOR_EVENT_USB_CONTROL, NULL);
}

static void event_from_isr(collector_event_type_t type, void * p_frame)
{
    if (s_event_queue == NULL)
    {
        return;
    }

    if ((type == COLLECTOR_EVENT_FRAME) && s_frame_event_pending)
    {
        return;
    }

    collector_event_t event = {.type = type, .p_frame = p_frame, .tick = xTaskGetTickCountFromISR()};
    BaseType_t task_woken = pdFALSE;
    if (xQueueSendFromISR(s_event_queue, &event, &task_woken) == pdPASS)
    {
        if (type == COLLECTOR_EVENT_FRAME)
        {
            s_frame_event_pending = true;
        }
    }
    else if (type == COLLECTOR_EVENT_FRAME)
    {
        s_context.statistics.dropped_frames++;
    }
    portYIELD_FROM_ISR(task_woken);
}

static void collector_task(void * p_context)
{
    FSP_PARAMETER_NOT_USED(p_context);
    bool platform_ready = collector_platform_initialize();
    collector_state_set_usb_ready(&s_context, platform_ready && collector_platform_usb_ready());
    collector_platform_status_set((uint32_t) s_context.state);

    while (true)
    {
        collector_platform_usb_diagnostics_poll();
        collector_event_t event;
        if (xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(100U)) != pdPASS)
        {
            collector_state_set_usb_ready(&s_context, collector_platform_usb_ready());
            if (s_context.state == COLLECTOR_STATE_STOPPING)
            {
                session_stop(xTaskGetTickCount());
            }
            continue;
        }

        if ((event.type == COLLECTOR_EVENT_TOGGLE) &&
            ((event.tick - s_last_toggle_tick) >= pdMS_TO_TICKS(COLLECTOR_DEBOUNCE_MS)))
        {
            s_last_toggle_tick = event.tick;
            if (s_context.state == COLLECTOR_STATE_WAITING)
            {
                session_start(event.tick);
            }
            else if (collector_state_stop(&s_context))
            {
                session_stop(event.tick);
            }
        }
        else if ((event.type == COLLECTOR_EVENT_FEED_MARKER) &&
                 ((event.tick - s_last_marker_tick) >= pdMS_TO_TICKS(COLLECTOR_DEBOUNCE_MS)))
        {
            s_last_marker_tick = event.tick;
            if (collector_state_feed_marker(&s_context))
            {
                uint32_t timestamp = collector_state_timestamp(&s_context, tick_to_ms(event.tick));
                (void) record_write(COLLECTOR_RECORD_FEED_MARKER, timestamp, NULL, 0U);
            }
        }
        else if (event.type == COLLECTOR_EVENT_USB_CONTROL)
        {
            collector_platform_usb_diagnostics_poll();
        }
        else if (event.type == COLLECTOR_EVENT_FRAME)
        {
            frame_process(&event);
            s_frame_event_pending = false;
        }

        collector_platform_status_set((uint32_t) s_context.state);
    }
}

static void session_start(TickType_t tick)
{
    collector_state_set_usb_ready(&s_context, collector_platform_usb_ready());
    uint32_t session_id = (uint32_t) tick;
    if (!collector_state_start(&s_context, tick_to_ms(tick), session_id))
    {
        return;
    }

    s_last_sample_tick = tick - pdMS_TO_TICKS(COLLECTOR_SAMPLE_PERIOD_MS);
    char payload[96];
    int length = snprintf(payload, sizeof(payload),
                          "{\"width\":640,\"height\":480,\"fps\":5,\"format\":\"RGB565\",\"jpeg_on_pc\":true}");
    if (length > 0)
    {
        (void) record_write(COLLECTOR_RECORD_SESSION_START, 0U, payload, (uint32_t) length);
    }
}

static void session_stop(TickType_t tick)
{
    uint32_t timestamp = collector_state_timestamp(&s_context, tick_to_ms(tick));
    collector_statistics_t statistics = s_context.statistics;
    (void) record_write(COLLECTOR_RECORD_SESSION_END, timestamp, &statistics, sizeof(statistics));
    collector_state_stopped(&s_context);
}

static void frame_process(const collector_event_t * p_event)
{
    if ((s_context.state != COLLECTOR_STATE_RECORDING) ||
        ((p_event->tick - s_last_sample_tick) < pdMS_TO_TICKS(COLLECTOR_SAMPLE_PERIOD_MS)))
    {
        return;
    }

    s_last_sample_tick = p_event->tick;
    s_context.statistics.captured_frames++;
    const uint8_t * p_source = p_event->p_frame;
    for (uint32_t row = 0; row < COLLECTOR_FRAME_HEIGHT; row++)
    {
        memcpy(s_frame_buffer + (row * COLLECTOR_FRAME_ROW_BYTES),
               p_source + (row * COLLECTOR_CAPTURE_STRIDE),
               COLLECTOR_FRAME_ROW_BYTES);
    }

    collector_frame_metadata_t * p_metadata = (collector_frame_metadata_t *) s_raw_buffer;
    memcpy(s_raw_buffer + sizeof(*p_metadata), s_frame_buffer, sizeof(s_frame_buffer));
    *p_metadata = (collector_frame_metadata_t)
    {
        .width = COLLECTOR_FRAME_WIDTH,
        .height = COLLECTOR_FRAME_HEIGHT,
        .quality = 0U,
        .reserved = {0U, 0U, 0U},
        .encode_time_us = 0U,
        .reserved2 = 0U
    };
    uint32_t timestamp = collector_state_timestamp(&s_context, tick_to_ms(p_event->tick));
    uint32_t packet_size = (uint32_t) sizeof(s_raw_buffer);
    if (record_write(COLLECTOR_RECORD_RAW_FRAME, timestamp, s_raw_buffer, packet_size))
    {
        s_context.statistics.encoded_frames++;
        s_context.statistics.transmitted_frames++;
    }
    else
    {
        s_context.statistics.dropped_frames++;
    }
}

static bool record_write(collector_record_type_t type, uint32_t timestamp_ms,
                         const void * p_payload, uint32_t payload_size)
{
    collector_record_header_t header;
    collector_protocol_header_build(&header, type, s_context.session_id,
                                    collector_state_next_sequence(&s_context), timestamp_ms,
                                    p_payload, payload_size);
    return collector_platform_usb_write(&header, sizeof(header)) &&
           ((payload_size == 0U) || collector_platform_usb_write(p_payload, payload_size));
}

static uint32_t tick_to_ms(TickType_t tick)
{
    return (uint32_t) (tick * portTICK_PERIOD_MS);
}

static void error_write(uint32_t timestamp_ms, const char * p_code)
{
    char payload[64];
    int length = snprintf(payload, sizeof(payload), "{\"code\":\"%s\"}", p_code);
    if (length > 0)
    {
        (void) record_write(COLLECTOR_RECORD_ERROR, timestamp_ms, payload, (uint32_t) length);
    }
}