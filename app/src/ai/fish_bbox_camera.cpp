#include "fish_bbox_camera.hpp"

#include "hal_data.h"
#include "r_capture_api.h"
#include "r_gpt.h"
#include "r_mipi_csi_api.h"
#include "r_vin.h"

extern "C"
{
#include <tk/tkernel.h>
#include <tk/syscall.h>
#include <tm/tmonitor.h>
#include <sysdepend/ra_fsp/device/hal_i2c/hal_i2c.h>
}

#include <cstdint>

namespace
{
constexpr uint32_t kCameraWidth = 640U;
constexpr uint32_t kCameraHeight = 480U;
constexpr uint32_t kCameraStrideBytes = 2048U;
constexpr uint32_t kCameraBytesPerPixel = 2U;
constexpr uint32_t kModelInputWidth = 256U;
constexpr uint32_t kModelInputHeight = 256U;
constexpr uint32_t kSubpixelOne = 1U << 16U;
constexpr UW kOv5640Address = 0x3cU;
constexpr bsp_io_port_pin_t kCameraEnablePin = BSP_IO_PORT_01_PIN_08;
constexpr bsp_io_port_pin_t kCameraPowerDownPin = BSP_IO_PORT_07_PIN_04;
constexpr bsp_io_port_pin_t kCameraResetPin = BSP_IO_PORT_07_PIN_09;

struct RegisterValue
{
    uint16_t address;
    uint8_t value;
};

ID g_i2c_device = 0;
uint8_t * volatile g_latest_frame = nullptr;
volatile uint32_t g_frame_sequence = 0U;
volatile uint32_t g_vin_callback_count = 0U;
volatile uint32_t g_vin_last_event = 0U;
volatile uint32_t g_vin_last_event_status = 0U;
volatile uint32_t g_vin_last_interrupt_status = 0U;
volatile uint32_t g_vin_last_buffer = 0U;
volatile uint32_t g_csi_callback_count = 0U;
volatile uint32_t g_csi_last_event = 0U;
volatile uint32_t g_csi_last_event_idx = 0U;
volatile uint32_t g_csi_last_status = 0U;
volatile uint32_t g_vin_raw_module_status = 0U;
volatile uint32_t g_vin_raw_interrupt_status = 0U;
volatile uint32_t g_csi_raw_receive_status = 0U;
volatile uint32_t g_csi_raw_interrupt_status = 0U;
volatile uint32_t g_csi_raw_lane0_status = 0U;
volatile uint32_t g_csi_raw_lane1_status = 0U;
volatile uint32_t g_csi_raw_virtual_channel_status = 0U;
volatile uint32_t g_csi_raw_control_status = 0U;
constexpr uint32_t kIsrErrorStormThreshold = 1000U;
volatile bool g_frame_capture_stopped = false;
volatile bool g_csi_irqs_masked_for_capture = false;
volatile uint32_t g_vin_error_event_count = 0U;
volatile bool g_vin_error_irq_disabled = false;
volatile uint32_t g_csi_error_event_count = 0U;
volatile bool g_csi_error_irq_disabled = false;
bool g_isr_storm_reported = false;
bool g_csi_delayed_recovery_attempted = false;
vin_extended_cfg_t g_camera_vin_extend{};
capture_cfg_t g_camera_vin_cfg{};

const RegisterValue kSensorSettings[] = {
    {0x3103, 0x11}, {0x3008, 0x82}, {0x3008, 0x42}, {0x3103, 0x03},
    {0x3017, 0x00}, {0x3018, 0x00}, {0x3034, 0x18}, {0x3037, 0x13},
    {0x3108, 0x01}, {0x3630, 0x36}, {0x3631, 0x0e}, {0x3632, 0xe2},
    {0x3633, 0x12}, {0x3621, 0xe0}, {0x3704, 0xa0}, {0x3703, 0x5a},
    {0x3715, 0x78}, {0x3717, 0x01}, {0x370b, 0x60}, {0x3705, 0x1a},
    {0x3905, 0x02}, {0x3906, 0x10}, {0x3901, 0x0a}, {0x3731, 0x12},
    {0x3600, 0x08}, {0x3601, 0x33}, {0x302d, 0x60}, {0x3620, 0x52},
    {0x371b, 0x20}, {0x471c, 0x50}, {0x3a13, 0x43}, {0x3a18, 0x00},
    {0x3a19, 0xf8}, {0x3635, 0x13}, {0x3636, 0x03}, {0x3634, 0x40},
    {0x3622, 0x01}, {0x3c01, 0x34}, {0x3c04, 0x28}, {0x3c05, 0x98},
    {0x3c06, 0x00}, {0x3c08, 0x00}, {0x3c09, 0x1c}, {0x3c0a, 0x9c},
    {0x3c0b, 0x40}, {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00},
    {0x3803, 0x04}, {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x07},
    {0x3807, 0x9f}, {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00},
    {0x3813, 0x00}, {0x3708, 0x64}, {0x3a08, 0x01}, {0x4001, 0x02},
    {0x4005, 0x1a}, {0x3000, 0x00}, {0x3002, 0x1c}, {0x3004, 0xff},
    {0x3006, 0xc3}, {0x300e, 0x45}, {0x302e, 0x08}, {0x4300, 0x32},
    {0x3034, 0x18}, {0x501f, 0x00}, {0x4407, 0x04}, {0x440e, 0x00},
    {0x5000, 0xa7}, {0x503d, 0x04}, {0x4800, 0x04}, {0x3007, 0xfb},
    {0x3017, 0x00}, {0x301d, 0xff}, {0x5001, 0xa3}, {0x3035, 0x12},
    {0x3036, 0xf6}, {0x3037, 0x08}, {0x3108, 0x12},
    {0x3008, 0x02}, {0x3c07, 0x08}, {0x3820, 0x40}, {0x3821, 0x01},
    {0x3814, 0x31}, {0x3815, 0x31}, {0x3803, 0x04}, {0x3808, 0x04},
    {0x3809, 0x00}, {0x380a, 0x02}, {0x380b, 0x58}, {0x380c, 0x05},
    {0x380d, 0xb5}, {0x380e, 0x04}, {0x380f, 0x47}, {0x3813, 0x06},
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3709, 0x52}, {0x370c, 0x03},
    {0x3a02, 0x03}, {0x3a03, 0xd8}, {0x3a09, 0x27}, {0x3a0a, 0x00},
    {0x3a0b, 0xf6}, {0x3a0e, 0x03}, {0x3a0d, 0x04}, {0x3a14, 0x03},
    {0x3a15, 0xd8}, {0x4004, 0x02}, {0x4713, 0x03}, {0x460b, 0x35},
    {0x460c, 0x22}, {0x4837, 0x0a}, {0x3824, 0x02}, {0x5001, 0xa3},
    {0x5180, 0xff}, {0x5181, 0xf2}, {0x5182, 0x00}, {0x5183, 0x14},
    {0x5184, 0x25}, {0x5185, 0x24}, {0x5186, 0x09}, {0x5187, 0x09},
    {0x5188, 0x09}, {0x5189, 0x75}, {0x518a, 0x54}, {0x518b, 0xe0},
    {0x518c, 0xb2}, {0x518d, 0x42}, {0x518e, 0x3d}, {0x518f, 0x56},
    {0x5190, 0x46}, {0x5191, 0xf8}, {0x5192, 0x04}, {0x5193, 0x70},
    {0x5194, 0xf0}, {0x5195, 0xf0}, {0x5196, 0x03}, {0x5197, 0x01},
    {0x5198, 0x04}, {0x5199, 0x12}, {0x519a, 0x04}, {0x519b, 0x00},
    {0x519c, 0x06}, {0x519d, 0x82}, {0x519e, 0x38},
    {0x5381, 0x1e}, {0x5382, 0x5b}, {0x5383, 0x08}, {0x5384, 0x0a},
    {0x5385, 0x7e}, {0x5386, 0x88}, {0x5387, 0x7c}, {0x5388, 0x6c},
    {0x5389, 0x10}, {0x538a, 0x01}, {0x538b, 0x98},
    {0x5300, 0x08}, {0x5301, 0x30}, {0x5302, 0x10}, {0x5303, 0x00},
    {0x5304, 0x08}, {0x5305, 0x30}, {0x5306, 0x08}, {0x5307, 0x16},
    {0x5309, 0x08}, {0x530a, 0x30}, {0x530b, 0x04}, {0x530c, 0x06},
    {0x5480, 0x01}, {0x5481, 0x08}, {0x5482, 0x14}, {0x5483, 0x28},
    {0x5484, 0x51}, {0x5485, 0x65}, {0x5486, 0x71}, {0x5487, 0x7d},
    {0x5488, 0x87}, {0x5489, 0x91}, {0x548a, 0x9a}, {0x548b, 0xaa},
    {0x548c, 0xb8}, {0x548d, 0xcd}, {0x548e, 0xdd}, {0x548f, 0xea},
    {0x5490, 0x1d},
    {0x5580, 0x06}, {0x5583, 0x40}, {0x5584, 0x10}, {0x5589, 0x10},
    {0x558a, 0x00}, {0x558b, 0xf8}, {0x501d, 0x04},
    {0x5800, 0x23}, {0x5801, 0x14}, {0x5802, 0x0f}, {0x5803, 0x0f},
    {0x5804, 0x12}, {0x5805, 0x26}, {0x5806, 0x0c}, {0x5807, 0x08},
    {0x5808, 0x05}, {0x5809, 0x05}, {0x580a, 0x08}, {0x580b, 0x0d},
    {0x580c, 0x08}, {0x580d, 0x03}, {0x580e, 0x00}, {0x580f, 0x00},
    {0x5810, 0x03}, {0x5811, 0x09}, {0x5812, 0x07}, {0x5813, 0x03},
    {0x5814, 0x00}, {0x5815, 0x01}, {0x5816, 0x03}, {0x5817, 0x08},
    {0x5818, 0x0d}, {0x5819, 0x08}, {0x581a, 0x05}, {0x581b, 0x06},
    {0x581c, 0x08}, {0x581d, 0x0e}, {0x581e, 0x29}, {0x581f, 0x17},
    {0x5820, 0x11}, {0x5821, 0x11}, {0x5822, 0x15}, {0x5823, 0x28},
    {0x5824, 0x46}, {0x5825, 0x26}, {0x5826, 0x08}, {0x5827, 0x26},
    {0x5828, 0x64}, {0x5829, 0x26}, {0x582a, 0x24}, {0x582b, 0x22},
    {0x582c, 0x24}, {0x582d, 0x24}, {0x582e, 0x06}, {0x582f, 0x22},
    {0x5830, 0x40}, {0x5831, 0x42}, {0x5832, 0x24}, {0x5833, 0x26},
    {0x5834, 0x24}, {0x5835, 0x22}, {0x5836, 0x22}, {0x5837, 0x26},
    {0x5838, 0x44}, {0x5839, 0x24}, {0x583a, 0x26}, {0x583b, 0x28},
    {0x583c, 0x42}, {0x583d, 0xce}, {0x5025, 0x00},
    {0x3a0f, 0x30}, {0x3a10, 0x28}, {0x3a1b, 0x30}, {0x3a1e, 0x26},
    {0x3a11, 0x60}, {0x3a1f, 0x14}, {0x4800, 0x04}, {0x3007, 0xfb},
    {0x3017, 0x00}, {0x301d, 0xff},
};

bool report_fsp_error(const char *operation, fsp_err_t error)
{
    if (error == FSP_SUCCESS)
    {
        return true;
    }
    tm_printf((UB *)"Camera %s failed: %d\n", (UB *)operation, static_cast<int>(error));
    return false;
}

bool write_sensor_register(uint16_t address, uint8_t value)
{
    return hal_i2c_write_reg16(g_i2c_device, kOv5640Address, address, value) >= E_OK;
}

bool read_sensor_register(uint16_t address, uint8_t *value)
{
    return hal_i2c_read_reg16(g_i2c_device, kOv5640Address, address, value) >= E_OK;
}

bool reset_sensor_before_identification()
{
    uint8_t system_control = 0U;
    if (!read_sensor_register(0x3103U, &system_control))
    {
        return false;
    }

    system_control = static_cast<uint8_t>(system_control & ~0x02U);
    return write_sensor_register(0x3103U, system_control) &&
           write_sensor_register(0x3008U, 0x82U);
}

bool apply_sensor_settings()
{
    for (const RegisterValue &setting : kSensorSettings)
    {
        if (!write_sensor_register(setting.address, setting.value))
        {
            tm_printf((UB *)"Camera I2C write failed at 0x%04x\n", setting.address);
            return false;
        }
        if (setting.address == 0x3008U && setting.value == 0x82U)
        {
            tk_dly_tsk(5);
        }
    }

    return true;
}

void report_capture_start_state(const char *stage)
{
    uint8_t sensor_stream = 0U;
    uint8_t sensor_mipi_stream = 0U;
    uint8_t sensor_mipi_control = 0U;
    uint8_t sensor_input_format = 0U;
    uint8_t sensor_pll_control_1 = 0U;
    uint8_t sensor_pll_control_2 = 0U;
    uint8_t sensor_pll_control_3 = 0U;
    uint8_t sensor_system_divider = 0U;
    uint8_t sensor_output_format = 0U;
    uint8_t sensor_mipi_control_2 = 0U;
    uint8_t sensor_mipi_timing = 0U;
    uint8_t sensor_output_width_high = 0U;
    uint8_t sensor_output_width_low = 0U;
    uint8_t sensor_output_height_high = 0U;
    uint8_t sensor_output_height_low = 0U;
    const bool sensor_read_ok =
        read_sensor_register(0x3008U, &sensor_stream) &&
        read_sensor_register(0x4202U, &sensor_mipi_stream) &&
        read_sensor_register(0x300eU, &sensor_mipi_control) &&
        read_sensor_register(0x3034U, &sensor_input_format) &&
        read_sensor_register(0x3035U, &sensor_pll_control_1) &&
        read_sensor_register(0x3036U, &sensor_pll_control_2) &&
        read_sensor_register(0x3037U, &sensor_pll_control_3) &&
        read_sensor_register(0x3108U, &sensor_system_divider) &&
        read_sensor_register(0x4300U, &sensor_output_format) &&
        read_sensor_register(0x4814U, &sensor_mipi_control_2) &&
        read_sensor_register(0x4837U, &sensor_mipi_timing) &&
        read_sensor_register(0x3808U, &sensor_output_width_high) &&
        read_sensor_register(0x3809U, &sensor_output_width_low) &&
        read_sensor_register(0x380aU, &sensor_output_height_high) &&
        read_sensor_register(0x380bU, &sensor_output_height_low);
    tm_printf((UB *)"Camera sensor state %s: ok=%u stream=%02x/%02x mipi=%02x input=%02x pll=%02x/%02x/%02x div=%02x format=%02x link=%02x timing=%02x\n",
              (UB *)stage,
              static_cast<unsigned int>(sensor_read_ok),
              static_cast<unsigned int>(sensor_stream),
              static_cast<unsigned int>(sensor_mipi_stream),
              static_cast<unsigned int>(sensor_mipi_control),
              static_cast<unsigned int>(sensor_input_format),
              static_cast<unsigned int>(sensor_pll_control_1),
              static_cast<unsigned int>(sensor_pll_control_2),
              static_cast<unsigned int>(sensor_pll_control_3),
              static_cast<unsigned int>(sensor_system_divider),
              static_cast<unsigned int>(sensor_output_format),
              static_cast<unsigned int>(sensor_mipi_control_2),
              static_cast<unsigned int>(sensor_mipi_timing));
    tm_printf((UB *)"Camera sensor size registers: width=%02x/%02x height=%02x/%02x\n",
              static_cast<unsigned int>(sensor_output_width_high),
              static_cast<unsigned int>(sensor_output_width_low),
              static_cast<unsigned int>(sensor_output_height_high),
              static_cast<unsigned int>(sensor_output_height_low));
    tm_printf((UB *)"Camera start state %s: VIN mc=%08x fc=%08x ie=%08x csi mct0=%08x mct2=%08x mct3=%08x dtel=%08x rxie=%08x mist=%08x phy pwr=%08x sfr=%08x ocr=%08x\n",
              (UB *)stage,
              static_cast<unsigned int>(R_VIN->MC),
              static_cast<unsigned int>(R_VIN->FC),
              static_cast<unsigned int>(R_VIN->IE),
              static_cast<unsigned int>(R_MIPI_CSI->MCT0),
              static_cast<unsigned int>(R_MIPI_CSI->MCT2),
              static_cast<unsigned int>(R_MIPI_CSI->MCT3),
              static_cast<unsigned int>(R_MIPI_CSI->DTEL),
              static_cast<unsigned int>(R_MIPI_CSI->RXIE),
              static_cast<unsigned int>(R_MIPI_CSI->MIST),
              static_cast<unsigned int>(R_MIPI_PHY->DPHYPWRCR),
              static_cast<unsigned int>(R_MIPI_PHY->DPHYSFR),
              static_cast<unsigned int>(R_MIPI_PHY->DPHYOCR));
}

bool configure_camera_pins()
{
    R_BSP_PinAccessEnable();
    fsp_err_t error = R_IOPORT_PinCfg(
        g_ioport.p_ctrl, kCameraPowerDownPin,
        IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    if (error == FSP_SUCCESS)
    {
        error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraEnablePin, BSP_IO_LEVEL_LOW);
    }
    R_BSP_PinAccessDisable();
    if (!report_fsp_error("power-down pin config", error))
    {
        return false;
    }

    R_BSP_PinAccessEnable();
    error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraPowerDownPin, BSP_IO_LEVEL_HIGH);
    if (error == FSP_SUCCESS)
    {
        error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraResetPin, BSP_IO_LEVEL_LOW);
    }
    R_BSP_PinAccessDisable();
    return report_fsp_error("reset pins", error);
}

uint8_t rgb565_to_grayscale(const uint8_t *frame, uint32_t x, uint32_t y)
{
    const uint32_t pixel_offset = y * kCameraStrideBytes + x * kCameraBytesPerPixel;
    const uint16_t pixel = static_cast<uint16_t>(frame[pixel_offset]) |
                           (static_cast<uint16_t>(frame[pixel_offset + 1U]) << 8U);
    const uint8_t red = static_cast<uint8_t>(((pixel >> 11U) & 0x1fU) * 255U / 31U);
    const uint8_t green = static_cast<uint8_t>(((pixel >> 5U) & 0x3fU) * 255U / 63U);
    const uint8_t blue = static_cast<uint8_t>((pixel & 0x1fU) * 255U / 31U);
    return static_cast<uint8_t>((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
}

uint8_t resize_grayscale_pixel(const uint8_t *frame, uint32_t output_x, uint32_t output_y)
{
    const uint32_t x_position_q16 = (output_x * 10U + 3U) << 14U;
    const uint32_t y_position_q16 = output_y * 122880U + 28672U;
    const uint32_t source_x0 = static_cast<uint32_t>(x_position_q16 >> 16U);
    const uint32_t source_y0 = static_cast<uint32_t>(y_position_q16 >> 16U);
    const uint32_t source_x1 = source_x0 + 1U;
    const uint32_t source_y1 = source_y0 + 1U;
    const uint32_t x_fraction = static_cast<uint32_t>(x_position_q16) & 0xffffU;
    const uint32_t y_fraction = static_cast<uint32_t>(y_position_q16) & 0xffffU;

    const uint64_t top =
        static_cast<uint64_t>(rgb565_to_grayscale(frame, source_x0, source_y0)) *
            (kSubpixelOne - x_fraction) +
        static_cast<uint64_t>(rgb565_to_grayscale(frame, source_x1, source_y0)) * x_fraction;
    const uint64_t bottom =
        static_cast<uint64_t>(rgb565_to_grayscale(frame, source_x0, source_y1)) *
            (kSubpixelOne - x_fraction) +
        static_cast<uint64_t>(rgb565_to_grayscale(frame, source_x1, source_y1)) * x_fraction;
    const uint64_t weighted = top * (kSubpixelOne - y_fraction) + bottom * y_fraction;
    return static_cast<uint8_t>((weighted + (1ULL << 31U)) >> 32U);
}

}

extern "C" void fish_bbox_vin_callback(capture_callback_args_t *args)
{
    if (args == nullptr)
    {
        return;
    }

    ++g_vin_callback_count;
    g_vin_last_event = args->event;
    g_vin_last_event_status = args->event_status;
    g_vin_last_interrupt_status = args->interrupt_status;
    g_vin_last_buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(args->p_buffer));
    if (args->event == VIN_EVENT_ERROR)
    {
        if (++g_vin_error_event_count >= kIsrErrorStormThreshold && !g_vin_error_irq_disabled)
        {
            NVIC_DisableIRQ(VECTOR_NUMBER_VIN_ERR);
            g_vin_error_irq_disabled = true;
        }
    }
    if (args->event == VIN_EVENT_NOTIFY && args->p_buffer != nullptr)
    {
        g_latest_frame = args->p_buffer;
        ++g_frame_sequence;
        R_VIN->MC_b.ME = 0;
        R_VIN->FC_b.CC = 0;
        g_frame_capture_stopped = true;
        if (!g_csi_irqs_masked_for_capture)
        {
            NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_RX);
            NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_DL);
            NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_VC);
            NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_PM);
            NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_GST);
            g_csi_irqs_masked_for_capture = true;
        }
    }
}

extern "C" void fish_bbox_mipi_csi_callback(mipi_csi_callback_args_t *args)
{
    if (args == nullptr)
    {
        return;
    }

    ++g_csi_callback_count;
    g_csi_last_event = static_cast<uint32_t>(args->event);
    g_csi_last_event_idx = args->event_idx;
    bool is_error_event = false;
    switch (args->event)
    {
    case MIPI_CSI_EVENT_DATA_LANE:
        g_csi_last_status = args->event_data.data_lane_status.mask;
        is_error_event = g_csi_last_status != 0U;
        break;
    case MIPI_CSI_EVENT_VIRTUAL_CHANNEL:
        g_csi_last_status = args->event_data.virtual_channel_status.mask;
        is_error_event =
            (g_csi_last_status & ~(R_MIPI_CSI_VCST0_FRS_Msk | R_MIPI_CSI_VCST0_FRD_Msk)) != 0U;
        break;
    case MIPI_CSI_EVENT_FRAME_DATA:
        g_csi_last_status = args->event_data.receive_status.mask;
        break;
    case MIPI_CSI_EVENT_POWER:
        g_csi_last_status = args->event_data.power_status.mask;
        break;
    case MIPI_CSI_EVENT_SHORT_PACKET_FIFO:
        g_csi_last_status = args->event_data.fifo_status.mask;
        is_error_event = g_csi_last_status != 0U;
        break;
    default:
        g_csi_last_status = 0U;
        break;
    }
    if (is_error_event &&
        ++g_csi_error_event_count >= kIsrErrorStormThreshold && !g_csi_error_irq_disabled)
    {
        NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_DL);
        NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_VC);
        NVIC_DisableIRQ(VECTOR_NUMBER_MIPICSI_GST);
        g_csi_error_irq_disabled = true;
    }
}

bool fish_bbox_camera_start()
{
    g_i2c_device = tk_opn_dev((const UB *)"hiica", TD_UPDATE);
    if (g_i2c_device <= 0)
    {
        tm_printf((UB *)"Camera I2C open failed: %d\n", static_cast<int>(g_i2c_device));
        return false;
    }

    g_camera_vin_extend = *static_cast<const vin_extended_cfg_t *>(g_vin0_cfg.p_extend);
    g_camera_vin_extend.input_ctrl.cfg_bits.scaling_enable = true;
    g_camera_vin_extend.conversion_ctrl.data_mode_bits.output_data_byte_swap = true;
    g_camera_vin_extend.conversion_data.uds_scale_bits.vertical_mask = 5120U;
    g_camera_vin_extend.conversion_data.uds_scale_bits.horizontal_mask = 6553U;
    g_camera_vin_extend.conversion_data.uds_clipping_bits.cl_vsize = kCameraHeight;
    g_camera_vin_extend.conversion_data.uds_clipping_bits.cl_hsize = kCameraWidth;
    g_camera_vin_cfg = g_vin0_cfg;
    g_camera_vin_cfg.p_extend = &g_camera_vin_extend;

    if (!report_fsp_error("VIN open", R_VIN_Open(&g_vin0_ctrl, &g_camera_vin_cfg)) ||
        !report_fsp_error("XCLK open", R_GPT_Open(&g_timer_camera_xclk_ctrl, &g_timer_camera_xclk_cfg)) ||
        !report_fsp_error("XCLK start", R_GPT_Start(&g_timer_camera_xclk_ctrl)) ||
        !configure_camera_pins())
    {
        return false;
    }

    tk_dly_tsk(300);
    R_BSP_PinAccessEnable();
    fsp_err_t error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraPowerDownPin, BSP_IO_LEVEL_LOW);
    R_BSP_PinAccessDisable();
    if (!report_fsp_error("power-up", error))
    {
        return false;
    }
    tk_dly_tsk(50);

    R_BSP_PinAccessEnable();
    error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraResetPin, BSP_IO_LEVEL_HIGH);
    R_BSP_PinAccessDisable();
    if (!report_fsp_error("reset release", error))
    {
        return false;
    }
    tk_dly_tsk(20);

    R_BSP_PinAccessEnable();
    error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraResetPin, BSP_IO_LEVEL_LOW);
    R_BSP_PinAccessDisable();
    if (!report_fsp_error("hardware reset", error))
    {
        return false;
    }
    tk_dly_tsk(20);

    R_BSP_PinAccessEnable();
    error = R_IOPORT_PinWrite(g_ioport.p_ctrl, kCameraResetPin, BSP_IO_LEVEL_HIGH);
    R_BSP_PinAccessDisable();
    if (!report_fsp_error("hardware reset release", error))
    {
        return false;
    }
    tk_dly_tsk(20);

    if (!reset_sensor_before_identification())
    {
        tm_putstring((UB *)"Camera software reset failed.\n");
        return false;
    }
    tk_dly_tsk(300);

    uint8_t product_id_high = 0U;
    uint8_t product_id_low = 0U;
    if (!read_sensor_register(0x300aU, &product_id_high) ||
        !read_sensor_register(0x300bU, &product_id_low))
    {
        tm_putstring((UB *)"Camera PID read failed.\n");
        return false;
    }
    tm_printf((UB *)"OV5640 PID=%02x%02x\n", product_id_high, product_id_low);
    if (product_id_high != 0x56U ||
        (product_id_low != 0x40U && product_id_low != 0x41U && product_id_low != 0x4cU))
    {
        tm_putstring((UB *)"OV5640 not detected.\n");
        return false;
    }

    if (!apply_sensor_settings())
    {
        return false;
    }
    uint8_t mipi_channel_control = 0U;
    if (!read_sensor_register(0x4814U, &mipi_channel_control) ||
        !write_sensor_register(0x4814U, static_cast<uint8_t>(mipi_channel_control & 0x3fU)))
    {
        tm_putstring((UB *)"Camera virtual channel configuration failed.\n");
        return false;
    }
    if (!write_sensor_register(0x4202U, 0x0fU) ||
        !write_sensor_register(0x3008U, 0x42U))
    {
        tm_putstring((UB *)"Camera stream standby failed.\n");
        return false;
    }
    tk_dly_tsk(1);

    g_latest_frame = nullptr;
    g_frame_sequence = 0U;
    g_csi_delayed_recovery_attempted = false;
    if (!report_fsp_error("capture start", R_VIN_CaptureStart(&g_vin0_ctrl, nullptr)))
    {
        return false;
    }
    report_capture_start_state("capture-ready");
    tk_dly_tsk(1);
    if (!write_sensor_register(0x3008U, 0x02U) ||
        !write_sensor_register(0x4202U, 0x00U))
    {
        tm_putstring((UB *)"Camera stream start failed.\n");
        return false;
    }

    if (R_MIPI_CSI->MCT3_b.RXEN == 0U)
    {
        tm_putstring((UB *)"Camera CSI RX was disabled after stream-on; restarting.\n");
        if (!report_fsp_error("CSI restart", R_MIPI_CSI_Start(&g_mipi_csi0_ctrl)))
        {
            return false;
        }
    }
    report_capture_start_state("stream-on");

    tm_putstring((UB *)"Camera capture started: VIN output 640x480 RGB565.\n");
    return true;
}

bool fish_bbox_camera_capture_start()
{
    if (R_VIN->MC_b.ME != 0U || R_VIN->FC_b.CC != 0U)
    {
        tm_putstring((UB *)"Cannot arm VIN: previous frame capture is still active.\n");
        return false;
    }    if (R_MIPI_CSI->MCT3_b.RXEN == 0U)
    {
        tm_putstring((UB *)"Cannot arm VIN: CSI receiver is not running.\n");
        return false;
    }

    g_latest_frame = nullptr;
    g_frame_capture_stopped = false;
    R_VIN->MC_b.ST = 1U;
    for (uint32_t cycle = 0U; cycle < 10U; ++cycle)
    {
        (void)R_VIN->MC_b.ST;
    }
    R_VIN->UDS_SCALE = g_camera_vin_extend.conversion_data.uds_scale_mask;
    R_VIN->MC_b.ME = 1U;
    R_VIN->FC_b.CC = 1U;
    return true;
}

const uint8_t *fish_bbox_camera_latest_frame()
{
    return g_latest_frame;
}

void fish_bbox_camera_report_status()
{
    g_vin_raw_module_status = R_VIN->MS;
    g_vin_raw_interrupt_status = R_VIN->INTS;
    g_csi_raw_receive_status = R_MIPI_CSI->RXST;
    g_csi_raw_interrupt_status = R_MIPI_CSI->MIST;
    g_csi_raw_lane0_status = R_MIPI_CSI->DLST0;
    g_csi_raw_lane1_status = R_MIPI_CSI->DLST1;
    g_csi_raw_virtual_channel_status = R_MIPI_CSI->VCST0;
    g_csi_raw_control_status = R_MIPI_CSI->MCT3;

    capture_status_t vin_status{};
    mipi_csi_status_t csi_status{};
    const fsp_err_t vin_error = R_VIN_StatusGet(&g_vin0_ctrl, &vin_status);
    const fsp_err_t csi_error = R_MIPI_CSI_StatusGet(&g_mipi_csi0_ctrl, &csi_status);
    tm_printf((UB *)"Camera diag: seq=%u VIN=%d/%u bytes=%u buffer=%08x cb=%u event=%u ms=%08x irq=%08x cbuff=%08x CSI=%d/%u cb=%u event=%u[%u] status=%08x rawVIN=%08x/%08x rawCSI=%08x/%08x lane=%08x/%08x vc=%08x ctl=%08x\n",
              static_cast<unsigned int>(g_frame_sequence),
              static_cast<int>(vin_error), static_cast<unsigned int>(vin_status.state),
              static_cast<unsigned int>(vin_status.data_size),
              static_cast<unsigned int>(reinterpret_cast<uintptr_t>(vin_status.p_buffer)),
              static_cast<unsigned int>(g_vin_callback_count),
              static_cast<unsigned int>(g_vin_last_event),
              static_cast<unsigned int>(g_vin_last_event_status),
              static_cast<unsigned int>(g_vin_last_interrupt_status),
              static_cast<unsigned int>(g_vin_last_buffer),
              static_cast<int>(csi_error), static_cast<unsigned int>(csi_status.state),
              static_cast<unsigned int>(g_csi_callback_count),
              static_cast<unsigned int>(g_csi_last_event),
              static_cast<unsigned int>(g_csi_last_event_idx),
              static_cast<unsigned int>(g_csi_last_status),
              static_cast<unsigned int>(g_vin_raw_module_status),
              static_cast<unsigned int>(g_vin_raw_interrupt_status),
              static_cast<unsigned int>(g_csi_raw_receive_status),
              static_cast<unsigned int>(g_csi_raw_interrupt_status),
              static_cast<unsigned int>(g_csi_raw_lane0_status),
              static_cast<unsigned int>(g_csi_raw_lane1_status),
              static_cast<unsigned int>(g_csi_raw_virtual_channel_status),
              static_cast<unsigned int>(g_csi_raw_control_status));
}

bool fish_bbox_camera_prepare_input(int8_t *output, size_t output_bytes,
                                    float input_scale, int32_t input_zero_point,
                                    uint32_t *frame_sequence)
{
    if (output == nullptr || output_bytes != kModelInputWidth * kModelInputHeight ||
        input_scale <= 0.0f || frame_sequence == nullptr)
    {
        return false;
    }

    if ((g_vin_error_irq_disabled || g_csi_error_irq_disabled) && !g_isr_storm_reported)
    {
        g_isr_storm_reported = true;
        tm_printf((UB *)"IRQ storm guard tripped: VIN err=%u disabled=%u CSI err=%u disabled=%u last=%u/%08x\n",
                  static_cast<unsigned int>(g_vin_error_event_count),
                  static_cast<unsigned int>(g_vin_error_irq_disabled),
                  static_cast<unsigned int>(g_csi_error_event_count),
                  static_cast<unsigned int>(g_csi_error_irq_disabled),
                  static_cast<unsigned int>(g_csi_last_event),
                  static_cast<unsigned int>(g_csi_last_status));
        fish_bbox_camera_report_status();
    }

    uint8_t *frame = nullptr;
    uint32_t sequence_before = 0U;
    uint32_t sequence_after = 0U;
    do
    {
        sequence_before = g_frame_sequence;
        frame = g_latest_frame;
        sequence_after = g_frame_sequence;
    } while (sequence_before != sequence_after);
    if (frame == nullptr || sequence_after == 0U)
    {
        if (!g_csi_delayed_recovery_attempted && R_MIPI_CSI->MCT3_b.RXEN == 0U)
        {
            g_csi_delayed_recovery_attempted = true;
            report_capture_start_state("rx-disabled");
            tm_putstring((UB *)"Camera CSI RX disabled while waiting; restarting once.\n");
            report_fsp_error("delayed CSI restart", R_MIPI_CSI_Start(&g_mipi_csi0_ctrl));
            report_capture_start_state("rx-restarted");
        }
        return false;
    }

    static bool first_frame_reported = false;
    if (!first_frame_reported)
    {
        tm_printf((UB *)"First camera frame received: seq=%u buffer=%08x\n",
                  static_cast<unsigned int>(sequence_after),
                  static_cast<unsigned int>(reinterpret_cast<uintptr_t>(frame)));
        if (g_frame_capture_stopped)
        {
            tm_putstring((UB *)"Single-frame capture complete; VIN stopped.\n");
        }
        if (g_csi_irqs_masked_for_capture)
        {
            tm_putstring((UB *)"CSI IRQs masked; CSI receiver remains active.\n");
        }
        tm_putstring((UB *)"Frame preprocessing v5 entered.\n");
        first_frame_reported = true;
    }

    static bool first_preprocessing_complete_reported = false;
    if (!first_preprocessing_complete_reported)
    {
        tm_putstring((UB *)"Resizing VIN frame with fixed-point bilinear.\n");
    }
    for (uint32_t y = 0; y < kModelInputHeight; ++y)
    {
        if (!first_preprocessing_complete_reported && y == 0U)
        {
            tm_putstring((UB *)"Writing first resized RGB565 grayscale row.\n");
        }
        for (uint32_t x = 0; x < kModelInputWidth; ++x)
        {
            const uint8_t grayscale = resize_grayscale_pixel(frame, x, y);
            const float normalized = static_cast<float>(grayscale) / 255.0f;
            int32_t quantized = static_cast<int32_t>(normalized / input_scale + 0.5f) + input_zero_point;
            if (quantized < -128)
            {
                quantized = -128;
            }
            else if (quantized > 127)
            {
                quantized = 127;
            }
            output[y * kModelInputWidth + x] = static_cast<int8_t>(quantized);
        }
        if (!first_preprocessing_complete_reported && ((y + 1U) % 16U) == 0U)
        {
            tm_printf((UB *)"Preprocessing rows %u/%u.\n",
                      static_cast<unsigned int>(y + 1U),
                      static_cast<unsigned int>(kModelInputHeight));
        }
    }

    *frame_sequence = sequence_after;
    if (!first_preprocessing_complete_reported)
    {
        tm_putstring((UB *)"Camera frame preprocessing complete.\n");
        first_preprocessing_complete_reported = true;
    }
    return true;
}