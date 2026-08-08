#if defined(COLLECTOR_FSP_IO_ENABLED)

#include "collector_platform.h"

#include "FreeRTOS.h"
#include "task.h"

#include "hal_data.h"
#include "r_usb_pcdc_api.h"

#include <limits.h>

#define COLLECTOR_JPEG_TIMEOUT_MS  (500U)
#define COLLECTOR_USB_TIMEOUT_MS   (1000U)
#define COLLECTOR_USB_CHUNK_SIZE   (16U * 1024U)
#define COLLECTOR_NOTIFY_JPEG_DONE (1UL << 0)
#define COLLECTOR_NOTIFY_JPEG_FAIL (1UL << 1)
#define COLLECTOR_NOTIFY_USB_DONE  (1UL << 2)
#define COLLECTOR_NOTIFY_USB_FAIL  (1UL << 3)

static TaskHandle_t        s_waiting_task;
static volatile bool       s_usb_ready;
static volatile uint32_t   s_jpeg_size;
static volatile TickType_t s_encode_start_tick;

void collector_jpeg_callback(jpeg_callback_args_t * p_args);
void collector_usb_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state);

bool collector_platform_initialize(void)
{
    fsp_err_t jpeg_error = R_JPEG_Open(g_jpeg0.p_ctrl, g_jpeg0.p_cfg);
    fsp_err_t usb_error = R_USB_Open(g_basic0.p_ctrl, g_basic0.p_cfg);
    return (jpeg_error == FSP_SUCCESS) && (usb_error == FSP_SUCCESS);
}

bool collector_platform_usb_ready(void)
{
    return s_usb_ready;
}

bool collector_platform_usb_write(const void * p_data, size_t size)
{
    uint8_t const * p_bytes = (uint8_t const *) p_data;
    size_t offset = 0U;
    s_waiting_task = xTaskGetCurrentTaskHandle();

    while (offset < size)
    {
        if (!s_usb_ready)
        {
            return false;
        }

        size_t remaining = size - offset;
        uint32_t chunk_size = (remaining > COLLECTOR_USB_CHUNK_SIZE) ?
                              COLLECTOR_USB_CHUNK_SIZE : (uint32_t) remaining;
        (void) ulTaskNotifyValueClear(NULL, COLLECTOR_NOTIFY_USB_DONE | COLLECTOR_NOTIFY_USB_FAIL);
        fsp_err_t error = R_USB_Write(g_basic0.p_ctrl, p_bytes + offset, chunk_size, USB_CLASS_PCDC);
        if (error != FSP_SUCCESS)
        {
            return false;
        }

        uint32_t notification = 0U;
        BaseType_t notified = xTaskNotifyWait(0U, COLLECTOR_NOTIFY_USB_DONE | COLLECTOR_NOTIFY_USB_FAIL,
                                              &notification, pdMS_TO_TICKS(COLLECTOR_USB_TIMEOUT_MS));
        if ((notified != pdTRUE) || ((notification & COLLECTOR_NOTIFY_USB_FAIL) != 0U))
        {
            return false;
        }
        offset += chunk_size;
    }

    return true;
}

bool collector_platform_jpeg_encode(const void * p_yuv422,
                                    size_t input_size,
                                    void * p_jpeg,
                                    size_t jpeg_capacity,
                                    size_t * p_jpeg_size,
                                    uint32_t * p_encode_time_us)
{
    if ((input_size > UINT32_MAX) || (jpeg_capacity > UINT32_MAX))
    {
        return false;
    }

    jpeg_encode_image_size_t image_size =
    {
        .horizontal_stride_pixels = 640U,
        .horizontal_resolution = 640U,
        .vertical_resolution = 480U
    };

    s_waiting_task = xTaskGetCurrentTaskHandle();
    s_jpeg_size = 0U;
    (void) ulTaskNotifyValueClear(NULL, COLLECTOR_NOTIFY_JPEG_DONE | COLLECTOR_NOTIFY_JPEG_FAIL);
    if ((R_JPEG_EncodeImageSizeSet(g_jpeg0.p_ctrl, &image_size) != FSP_SUCCESS) ||
        (R_JPEG_OutputBufferSet(g_jpeg0.p_ctrl, p_jpeg, (uint32_t) jpeg_capacity) != FSP_SUCCESS))
    {
        return false;
    }

    s_encode_start_tick = xTaskGetTickCount();
    if (R_JPEG_InputBufferSet(g_jpeg0.p_ctrl, (void *) p_yuv422, (uint32_t) input_size) != FSP_SUCCESS)
    {
        return false;
    }

    uint32_t notification = 0U;
    BaseType_t notified = xTaskNotifyWait(0U, COLLECTOR_NOTIFY_JPEG_DONE | COLLECTOR_NOTIFY_JPEG_FAIL,
                                          &notification, pdMS_TO_TICKS(COLLECTOR_JPEG_TIMEOUT_MS));
    if ((notified != pdTRUE) || ((notification & COLLECTOR_NOTIFY_JPEG_FAIL) != 0U))
    {
        (void) R_JPEG_Close(g_jpeg0.p_ctrl);
        return false;
    }

    *p_jpeg_size = (size_t) s_jpeg_size;
    TickType_t elapsed_ticks = xTaskGetTickCount() - s_encode_start_tick;
    *p_encode_time_us = (uint32_t) (elapsed_ticks * (1000000UL / configTICK_RATE_HZ));
    return true;
}

void collector_jpeg_callback(jpeg_callback_args_t * p_args)
{
    BaseType_t task_woken = pdFALSE;
    uint32_t notification = COLLECTOR_NOTIFY_JPEG_FAIL;
    if (p_args->status == JPEG_STATUS_OPERATION_COMPLETE)
    {
        s_jpeg_size = p_args->image_size;
        notification = COLLECTOR_NOTIFY_JPEG_DONE;
    }
    if (s_waiting_task != NULL)
    {
        (void) xTaskNotifyFromISR(s_waiting_task, notification, eSetBits, &task_woken);
        portYIELD_FROM_ISR(task_woken);
    }
}

void collector_usb_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state)
{
    FSP_PARAMETER_NOT_USED(task);
    FSP_PARAMETER_NOT_USED(state);

    if (p_event->event == USB_STATUS_CONFIGURED)
    {
        s_usb_ready = false;
    }
    else if ((p_event->event == USB_STATUS_DETACH) || (p_event->event == USB_STATUS_SUSPEND))
    {
        s_usb_ready = false;
    }
    else if ((p_event->event == USB_STATUS_REQUEST) &&
             ((p_event->setup.request_type & USB_BREQUEST) == USB_PCDC_SET_CONTROL_LINE_STATE))
    {
        s_usb_ready = (p_event->setup.request_value & 1U) != 0U;
    }
    else if ((p_event->event == USB_STATUS_WRITE_COMPLETE) && (s_waiting_task != NULL))
    {
        uint32_t notification = (p_event->status == FSP_SUCCESS) ?
                                COLLECTOR_NOTIFY_USB_DONE : COLLECTOR_NOTIFY_USB_FAIL;
        (void) xTaskNotify(s_waiting_task, notification, eSetBits);
    }
}

#endif