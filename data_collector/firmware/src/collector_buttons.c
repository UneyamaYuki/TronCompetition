#include "common_data.h"

#include "collector_app.h"

void button_irq12_ds_callback(external_irq_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    collector_app_toggle_from_isr();
}

void button_irq13_ds_callback(external_irq_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    collector_app_feed_marker_from_isr();
}