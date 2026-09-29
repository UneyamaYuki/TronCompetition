#include "ospi_model_storage.hpp"

#include "r_ospi_b.h"
#include "r_spi_flash_api.h"

extern "C"
{
#include <tm/tmonitor.h>
}

namespace
{
ospi_b_instance_ctrl_t g_ospi_model_ctrl{};

const ospi_b_timing_setting_t g_ospi_model_timing = {
    .command_to_command_interval = OSPI_B_COMMAND_INTERVAL_CLOCKS_2,
    .cs_pullup_lag = OSPI_B_COMMAND_CS_PULLUP_CLOCKS_NO_EXTENSION,
    .cs_pulldown_lead = OSPI_B_COMMAND_CS_PULLDOWN_CLOCKS_NO_EXTENSION,
    .sdr_drive_timing = OSPI_B_SDR_DRIVE_TIMING_BEFORE_CK,
    .sdr_sampling_edge = OSPI_B_CK_EDGE_FALLING,
    .sdr_sampling_delay = OSPI_B_SDR_SAMPLING_DELAY_NONE,
    .ddr_sampling_extension = OSPI_B_DDR_SAMPLING_EXTENSION_NONE,
};

const spi_flash_erase_command_t g_ospi_model_initial_erase_commands[] = {
    { .command = 0x21, .size = 4096 },
    { .command = 0xdc, .size = 262144 },
    { .command = 0x60, .size = SPI_FLASH_ERASE_SIZE_CHIP_ERASE },
};

const ospi_b_table_t g_ospi_model_initial_erase_table = {
    .p_table = (void *)g_ospi_model_initial_erase_commands,
    .length = sizeof(g_ospi_model_initial_erase_commands) /
              sizeof(g_ospi_model_initial_erase_commands[0]),
};

const ospi_b_xspi_command_set_t g_ospi_model_command_set_table[] = {
    {
        .protocol = SPI_FLASH_PROTOCOL_1S_1S_1S,
        .frame_format = OSPI_B_FRAME_FORMAT_STANDARD,
        .latency_mode = OSPI_B_LATENCY_MODE_FIXED,
        .command_bytes = OSPI_B_COMMAND_BYTES_1,
        .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,
        .read_command = 0x13,
        .program_command = 0x12,
        .write_enable_command = 0x06,
        .status_command = 0x05,
        .row_load_command = 0,
        .row_store_command = 0,
        .read_dummy_cycles = 0,
        .program_dummy_cycles = 0,
        .status_dummy_cycles = 0,
        .row_load_dummy_cycles = 0,
        .row_store_dummy_cycles = 0,
        .address_msb_mask = 0xf0,
        .status_needs_address = false,
        .status_address = 0U,
        .status_address_bytes = (spi_flash_address_bytes_t)0U,
        .p_erase_commands = &g_ospi_model_initial_erase_table,
    },
};

const ospi_b_table_t g_ospi_model_command_set = {
    .p_table = (void *)g_ospi_model_command_set_table,
    .length = 1,
};

const ospi_b_extended_cfg_t g_ospi_model_extended_cfg = {
    .ospi_b_unit = 0,
    .channel = OSPI_B_DEVICE_NUMBER_1,
    .p_timing_settings = &g_ospi_model_timing,
    .p_xspi_command_set = &g_ospi_model_command_set,
    .data_latch_delay_clocks = OSPI_B_DS_TIMING_DELAY_16,
    .p_autocalibration_preamble_pattern_addr = (uint8_t *)0x00,
};

const spi_flash_cfg_t g_ospi_model_cfg = {
    .spi_protocol = SPI_FLASH_PROTOCOL_1S_1S_1S,
    .read_mode = SPI_FLASH_READ_MODE_STANDARD,
    .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,
    .dummy_clocks = SPI_FLASH_DUMMY_CLOCKS_DEFAULT,
    .page_program_address_lines = (spi_flash_data_lines_t)0U,
    .write_status_bit = 0,
    .write_enable_bit = 1,
    .page_size_bytes = 64,
    .page_program_command = 0,
    .write_enable_command = 0,
    .status_command = 0,
    .read_command = 0,
    .xip_enter_command = 0U,
    .xip_exit_command = 0U,
    .erase_command_list_length = sizeof(g_ospi_model_initial_erase_commands) /
                                 sizeof(g_ospi_model_initial_erase_commands[0]),
    .p_erase_command_list = g_ospi_model_initial_erase_commands,
    .p_extend = &g_ospi_model_extended_cfg,
};

}

bool fish_bbox_model_storage_start()
{
    if (g_ospi_model_ctrl.open != 0U)
    {
        return true;
    }

    const fsp_err_t error = R_OSPI_B_Open(&g_ospi_model_ctrl, &g_ospi_model_cfg);
    if (error != FSP_SUCCESS)
    {
        tm_printf((UB *)"Model OSPI open failed: %d\n", static_cast<int>(error));
        return false;
    }

    R_XSPI0->LIOCTL_b.RSTCS0 = 0;
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    R_XSPI0->LIOCTL_b.RSTCS0 = 1;
    R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);

    tm_putstring((UB *)"Model OSPI CS1 SPI memory mapping enabled.\n");
    return true;
}
