#include "collector_platform.h"

#include "hal_data.h"

BSP_WEAK_REFERENCE bool collector_platform_initialize(void)
{
    return false;
}

BSP_WEAK_REFERENCE bool collector_platform_usb_ready(void)
{
    return false;
}

BSP_WEAK_REFERENCE bool collector_platform_usb_write(const void * p_data, size_t size)
{
    FSP_PARAMETER_NOT_USED(p_data);
    FSP_PARAMETER_NOT_USED(size);
    return false;
}

BSP_WEAK_REFERENCE bool collector_platform_jpeg_encode(const void * p_yuv422,
                                                       size_t input_size,
                                                       void * p_jpeg,
                                                       size_t jpeg_capacity,
                                                       size_t * p_jpeg_size,
                                                       uint32_t * p_encode_time_us)
{
    FSP_PARAMETER_NOT_USED(p_yuv422);
    FSP_PARAMETER_NOT_USED(input_size);
    FSP_PARAMETER_NOT_USED(p_jpeg);
    FSP_PARAMETER_NOT_USED(jpeg_capacity);
    FSP_PARAMETER_NOT_USED(p_jpeg_size);
    FSP_PARAMETER_NOT_USED(p_encode_time_us);
    return false;
}

BSP_WEAK_REFERENCE void collector_platform_status_set(uint32_t state)
{
    FSP_PARAMETER_NOT_USED(state);
}