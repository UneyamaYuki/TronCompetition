#if defined(COLLECTOR_FSP_IO_ENABLED)

#include <stdio.h>

#include "collector_platform.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_pin_cfg.h"
#include "collector_app.h"
#include "hal_data.h"
#include "r_usb_pcdc_api.h"
#include "jlink_console.h"

#define COLLECTOR_USB_TIMEOUT_MS   (1000U)
#define COLLECTOR_USB_CHUNK_SIZE   (16U * 1024U)
#define COLLECTOR_NOTIFY_USB_DONE  (1UL << 0)
#define COLLECTOR_NOTIFY_USB_FAIL  (1UL << 1)
#define COLLECTOR_USB_CONTROL_NONE (0U)
#define COLLECTOR_USB_CONTROL_GET  (1U)
#define COLLECTOR_USB_CONTROL_SET  (2U)
#define COLLECTOR_USB_LINE_CODING_SIZE (7U)

static TaskHandle_t        s_waiting_task;
static volatile bool       s_usb_ready;
static volatile uint32_t   s_usb_diag_sequence;
static volatile uint32_t   s_usb_diag_event;
static volatile uint32_t   s_usb_diag_status;
static volatile uint32_t   s_usb_diag_type;
static volatile uint32_t   s_usb_diag_module;
static volatile uint32_t   s_usb_diag_pipe;
static volatile uint32_t   s_usb_diag_data_size;
static volatile uint32_t   s_usb_diag_request_type;
static volatile uint32_t   s_usb_diag_request_value;
static volatile uint32_t   s_usb_diag_request_index;
static volatile uint32_t   s_usb_diag_request_length;
static volatile usb_event_info_t s_usb_control_event;
static volatile uint32_t   s_usb_control_operation;
static uint8_t             s_usb_line_coding[COLLECTOR_USB_LINE_CODING_SIZE] =
{
    0x00U, 0xC2U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U
};

void collector_usb_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state);

bool collector_platform_initialize(void)
{
    fsp_err_t usb_error = R_USB_Open(g_basic0.p_ctrl, g_basic0.p_cfg);
    bsp_io_level_t vbus_level = BSP_IO_LEVEL_LOW;
    fsp_err_t vbus_error = R_IOPORT_PinRead(g_ioport.p_ctrl, USB_HS_VBUS, &vbus_level);
    fsp_err_t pullup_error = FSP_ERR_NOT_OPEN;
    if ((usb_error == FSP_SUCCESS) &&
        (vbus_error == FSP_SUCCESS) &&
        (vbus_level == BSP_IO_LEVEL_HIGH))
    {
        (void) R_USB_PullUp(g_basic0.p_ctrl, USB_OFF);
        vTaskDelay(pdMS_TO_TICKS(50U));
        pullup_error = R_USB_PullUp(g_basic0.p_ctrl, USB_ON);
    }

    char_t message[112];
    (void) snprintf(message, sizeof(message),
                    "DC: R_USB_Open=%ld [diag-v2] VBUS=%u err=%ld pullup=%ld\r\n",
                    (long) usb_error, (unsigned int) vbus_level, (long) vbus_error,
                    (long) pullup_error);
    (void) print_to_console(message);
    return usb_error == FSP_SUCCESS;
}

bool collector_platform_usb_ready(void)
{
    return s_usb_ready;
}

void collector_platform_usb_diagnostics_poll(void)
{
    uint32_t control_operation = s_usb_control_operation;
    if (control_operation != COLLECTOR_USB_CONTROL_NONE)
    {
        usb_event_info_t control_event = s_usb_control_event;
        fsp_err_t control_error = FSP_ERR_USB_FAILED;
        if (control_operation == COLLECTOR_USB_CONTROL_GET)
        {
            control_error = R_USB_PeriControlDataSet((usb_ctrl_t *) &control_event,
                                                     s_usb_line_coding,
                                                     COLLECTOR_USB_LINE_CODING_SIZE);
        }
        else if (control_operation == COLLECTOR_USB_CONTROL_SET)
        {
            control_error = R_USB_PeriControlDataGet((usb_ctrl_t *) &control_event,
                                                     s_usb_line_coding,
                                                     COLLECTOR_USB_LINE_CODING_SIZE);
        }

        if (control_error == FSP_SUCCESS)
        {
            s_usb_control_operation = COLLECTOR_USB_CONTROL_NONE;
        }

        char_t control_message[80];
        (void) snprintf(control_message, sizeof(control_message),
                        "DC: USB control op=%lu err=%ld\r\n",
                        (unsigned long) control_operation, (long) control_error);
        (void) print_to_console(control_message);
    }

    static uint32_t reported_sequence;
    uint32_t sequence = s_usb_diag_sequence;
    if (sequence == reported_sequence)
    {
        return;
    }

    uint32_t event = s_usb_diag_event;
    uint32_t status = s_usb_diag_status;
    uint32_t type = s_usb_diag_type;
    uint32_t module = s_usb_diag_module;
    uint32_t pipe = s_usb_diag_pipe;
    uint32_t data_size = s_usb_diag_data_size;
    uint32_t request_type = s_usb_diag_request_type;
    uint32_t request_value = s_usb_diag_request_value;
    uint32_t request_index = s_usb_diag_request_index;
    uint32_t request_length = s_usb_diag_request_length;
    reported_sequence = sequence;

    char_t message[160];
    (void) snprintf(message, sizeof(message),
                    "DC: USB event=%lu status=%lu type=%lu mod=%lu pipe=%lu data=%lu req=%04lx val=%04lx idx=%04lx len=%04lx\r\n",
                    (unsigned long) event, (unsigned long) status, (unsigned long) type,
                    (unsigned long) module, (unsigned long) pipe, (unsigned long) data_size,
                    (unsigned long) request_type, (unsigned long) request_value,
                    (unsigned long) request_index, (unsigned long) request_length);
    (void) print_to_console(message);
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

void collector_usb_callback(usb_event_info_t * p_event, usb_hdl_t task, usb_onoff_t state)
{
    s_usb_diag_event = (uint32_t) p_event->event;
    s_usb_diag_status = (uint32_t) p_event->status;
    s_usb_diag_type = (uint32_t) p_event->type;
    s_usb_diag_module = (uint32_t) p_event->module_number;
    s_usb_diag_pipe = (uint32_t) p_event->pipe;
    s_usb_diag_data_size = p_event->data_size;
    s_usb_diag_request_type = p_event->setup.request_type;
    s_usb_diag_request_value = p_event->setup.request_value;
    s_usb_diag_request_index = p_event->setup.request_index;
    s_usb_diag_request_length = p_event->setup.request_length;
    s_usb_diag_sequence++;
    FSP_PARAMETER_NOT_USED(state);

    uint16_t request = p_event->setup.request_type & USB_BREQUEST;
    if ((p_event->event == USB_STATUS_REQUEST) && (request == USB_PCDC_GET_LINE_CODING))
    {
        s_usb_control_event = *p_event;
        s_usb_control_operation = COLLECTOR_USB_CONTROL_GET;
        collector_app_usb_control_from_isr();
    }
    else if ((p_event->event == USB_STATUS_REQUEST) && (request == USB_PCDC_SET_LINE_CODING))
    {
        s_usb_control_event = *p_event;
        s_usb_control_operation = COLLECTOR_USB_CONTROL_SET;
        collector_app_usb_control_from_isr();
    }
    else if ((p_event->event == USB_STATUS_REQUEST) && (request == USB_PCDC_SET_CONTROL_LINE_STATE))
    {
        s_usb_ready = (p_event->setup.request_value & 1U) != 0U;
    }
    else if (p_event->event == USB_STATUS_CONFIGURED)
    {
        s_usb_ready = false;
    }
    else if ((p_event->event == USB_STATUS_DETACH) || (p_event->event == USB_STATUS_SUSPEND))
    {
        s_usb_ready = false;
    }
    else if ((p_event->event == USB_STATUS_WRITE_COMPLETE) && (s_waiting_task != NULL))
    {
        BaseType_t task_woken = pdFALSE;
        uint32_t notification = (p_event->status == FSP_SUCCESS) ?
                                COLLECTOR_NOTIFY_USB_DONE : COLLECTOR_NOTIFY_USB_FAIL;
        (void) xTaskNotifyFromISR(s_waiting_task, notification, eSetBits, &task_woken);
        portYIELD_FROM_ISR(task_woken);
    }

    FSP_PARAMETER_NOT_USED(task);
}

#endif