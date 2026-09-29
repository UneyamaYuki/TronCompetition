#ifndef COLLECTOR_APP_H
#define COLLECTOR_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool collector_app_start(void);
void collector_app_frame_from_isr(void * p_frame);
void collector_app_toggle_from_isr(void);
void collector_app_feed_marker_from_isr(void);
void collector_app_usb_control_from_isr(void);

#endif