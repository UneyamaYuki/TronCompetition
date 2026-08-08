#include "common_init.h"

#include "board_cfg.h"

uint32_t g_pwm_dcs[3] =
{
    LED_INTENSITY_10,
    LED_INTENSITY_50,
    LED_INTENSITY_90
};

uint32_t g_pwm_rates[3] =
{
    BLINK_FREQ_1HZ,
    BLINK_FREQ_5HZ,
    BLINK_FREQ_10HZ
};

st_board_status_t g_board_status = {0};