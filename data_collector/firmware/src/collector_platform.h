#ifndef COLLECTOR_PLATFORM_H
#define COLLECTOR_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool collector_platform_initialize(void);
bool collector_platform_usb_ready(void);
bool collector_platform_usb_write(const void * p_data, size_t size);
void collector_platform_usb_diagnostics_poll(void);
void collector_platform_status_set(uint32_t state);

#endif