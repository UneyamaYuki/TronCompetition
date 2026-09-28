/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_usb_basic.h"
#include "r_usb_basic_api.h"
#include "r_usb_pcdc_api.h"
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_adc_b.h"
                      #include "r_adc_api.h"
#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include "r_sci_b_uart.h"
            #include "r_uart_api.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
FSP_HEADER
/* Basic on USB Instance. */
extern const usb_instance_t g_basic0;

/** Access the USB instance using these structures when calling API functions directly (::p_api is not used). */
extern usb_instance_ctrl_t g_basic0_ctrl;
extern const usb_cfg_t g_basic0_cfg;

#ifndef NULL
void NULL(void *);
#endif

#if 0 == BSP_CFG_RTOS
#ifndef collector_usb_callback
void collector_usb_callback(usb_callback_args_t *);
#endif
#endif

#if 2 == BSP_CFG_RTOS
#ifndef collector_usb_callback
void collector_usb_callback(usb_event_info_t *, usb_hdl_t, usb_onoff_t);
#endif
#endif
/** CDC Driver on USB Instance. */
/** Timer on GPT Instance. */
extern const timer_instance_t led_brightness_timer;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t led_brightness_timer_ctrl;
extern const timer_cfg_t led_brightness_timer_cfg;

#ifndef led_brightness_timer_callback
void led_brightness_timer_callback(timer_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t led_pulse_timer;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t led_pulse_timer_ctrl;
extern const timer_cfg_t led_pulse_timer_cfg;

#ifndef led_pulse_timer_callback
void led_pulse_timer_callback(timer_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_blinker_blue;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_blinker_blue_ctrl;
extern const timer_cfg_t g_blinker_blue_cfg;

#ifndef gpt_blinker_blue_callback
void gpt_blinker_blue_callback(timer_callback_args_t * p_args);
#endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_gpt_blue;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_gpt_blue_ctrl;
extern const timer_cfg_t g_gpt_blue_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/** ADC on ADC_B instance. */
                    extern const adc_instance_t g_adc;

                    /** Access the ADC_B instance using these structures when calling API functions directly (::p_api is not used). */
                    extern adc_b_instance_ctrl_t g_adc_ctrl;
                    extern const adc_cfg_t g_adc_cfg;
                    extern const adc_b_scan_cfg_t g_adc_scan_cfg;

                    #ifndef NULL
                    void NULL(adc_callback_args_t * p_args);
                    #endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_memory_performance;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_memory_performance_ctrl;
extern const timer_cfg_t g_memory_performance_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
#if OSPI_B_CFG_DMAC_SUPPORT_ENABLE
    #include "r_dmac.h"
#endif
#if OSPI_CFG_DOTF_SUPPORT_ENABLE
    #include "r_sce_if.h"
#endif

extern const spi_flash_instance_t g_ospi0;
extern ospi_b_instance_ctrl_t g_ospi0_ctrl;
extern const spi_flash_cfg_t g_ospi0_cfg;
/** UART on SCI Instance. */
            extern const uart_instance_t      g_jlink_console;

            /** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
            extern sci_b_uart_instance_ctrl_t     g_jlink_console_ctrl;
            extern const uart_cfg_t g_jlink_console_cfg;
            extern const sci_b_uart_extended_cfg_t g_jlink_console_cfg_extend;

            #ifndef jlink_console_callback
            void jlink_console_callback(uart_callback_args_t * p_args);
            #endif
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer_camera_xclk;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer_camera_xclk_ctrl;
extern const timer_cfg_t g_timer_camera_xclk_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_board_i2c_master;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_board_i2c_master_ctrl;
extern const i2c_master_cfg_t g_board_i2c_master_cfg;

#ifndef board_i2c_master_callback
void board_i2c_master_callback(i2c_master_callback_args_t * p_args);
#endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
