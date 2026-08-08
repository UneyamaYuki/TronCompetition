#ifndef COLLECTOR_PLATFORM_H
#define COLLECTOR_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool collector_platform_initialize(void);
bool collector_platform_usb_ready(void);
bool collector_platform_usb_write(const void * p_data, size_t size);
bool collector_platform_jpeg_encode(const void * p_yuv422,
                                    size_t input_size,
                                    void * p_jpeg,
                                    size_t jpeg_capacity,
                                    size_t * p_jpeg_size,
                                    uint32_t * p_encode_time_us);
void collector_platform_status_set(uint32_t state);

#endif