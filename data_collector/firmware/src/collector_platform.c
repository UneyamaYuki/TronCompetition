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

BSP_WEAK_REFERENCE void collector_platform_usb_diagnostics_poll(void)
{
}

BSP_WEAK_REFERENCE void collector_platform_status_set(uint32_t state)
{
    FSP_PARAMETER_NOT_USED(state);
}