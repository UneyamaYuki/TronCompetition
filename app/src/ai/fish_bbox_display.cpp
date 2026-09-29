#include "fish_bbox_display.hpp"

#include "hal_data.h"
#include "r_glcdc.h"

extern "C"
{
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
}

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint32_t kPanelWidth = 1024U;
constexpr uint32_t kPanelHeight = 600U;
constexpr uint32_t kCameraWidth = 640U;
constexpr uint32_t kCameraHeight = 480U;
constexpr uint32_t kCameraStrideBytes = 2048U;
constexpr uint32_t kPanelOffsetX = (kPanelWidth - kCameraWidth) / 2U;
constexpr uint32_t kPanelOffsetY = (kPanelHeight - kCameraHeight) / 2U;
constexpr float kModelInputSize = 256.0f;
constexpr uint32_t kBoxThickness = 3U;
constexpr uint16_t kBoxColorDetected = 0x07E0U;  // RGB565 green
constexpr uint16_t kFoodColor = 0xFFE0U;
constexpr uint16_t kStatusMonitoringColor = 0x07FFU;
constexpr uint16_t kStatusCompletedColor = 0x07E0U;
constexpr uint16_t kStatusAlertColor = 0xF800U;
constexpr uint16_t kStatusUnknownColor = 0xFFE0U;
constexpr bsp_io_port_pin_t kBacklightPin = BSP_IO_PORT_05_PIN_14;

uint16_t g_display_framebuffers[2][kPanelHeight][kPanelWidth]
    BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit_nocache");

glcdc_instance_ctrl_t g_display_ctrl;

const glcdc_extended_cfg_t g_display_extend_cfg = {
    .tcon_hsync = GLCDC_TCON_PIN_1,
    .tcon_vsync = GLCDC_TCON_PIN_0,
    .tcon_de = GLCDC_TCON_PIN_2,
    .correction_proc_order = GLCDC_CORRECTION_PROC_ORDER_BRIGHTNESS_CONTRAST2GAMMA,
    .clksrc = GLCDC_CLK_SRC_INTERNAL,
    .clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_7,
    .dithering_mode = GLCDC_DITHERING_MODE_TRUNCATE,
    .dithering_pattern_A = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_B = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_C = GLCDC_DITHERING_PATTERN_11,
    .dithering_pattern_D = GLCDC_DITHERING_PATTERN_11,
    .phy_layer = nullptr,
};

const display_cfg_t g_display_cfg = {
    .input =
    {
        {
            .p_base = reinterpret_cast<uint32_t *>(&g_display_framebuffers[0][0][0]),
            .hsize = kPanelWidth,
            .vsize = kPanelHeight,
            .hstride = kPanelWidth,
            .format = DISPLAY_IN_FORMAT_16BITS_RGB565,
            .line_descending_enable = false,
            .lines_repeat_enable = false,
            .lines_repeat_times = 0,
        },
        {
            .p_base = nullptr,
            .hsize = kPanelWidth,
            .vsize = kPanelHeight,
            .hstride = kPanelWidth,
            .format = DISPLAY_IN_FORMAT_16BITS_RGB565,
            .line_descending_enable = false,
            .lines_repeat_enable = false,
            .lines_repeat_times = 0,
        },
    },
    .output =
    {
        .htiming = {.total_cyc = 1334, .display_cyc = 1024, .back_porch = 300,
                    .sync_width = 10, .sync_polarity = DISPLAY_SIGNAL_POLARITY_LOACTIVE},
        .vtiming = {.total_cyc = 780, .display_cyc = 600, .back_porch = 30,
                    .sync_width = 5, .sync_polarity = DISPLAY_SIGNAL_POLARITY_LOACTIVE},
        .format = DISPLAY_OUT_FORMAT_24BITS_RGB888,
        .endian = DISPLAY_ENDIAN_LITTLE,
        .color_order = DISPLAY_COLOR_ORDER_RGB,
        .data_enable_polarity = DISPLAY_SIGNAL_POLARITY_HIACTIVE,
        .sync_edge = DISPLAY_SIGNAL_SYNC_EDGE_FALLING,
        .bg_color = {.byte = {.b = 0, .g = 0, .r = 0, .a = 255}},
        .brightness = {.enable = false, .r = 512, .g = 512, .b = 512},
        .contrast = {.enable = false, .r = 128, .g = 128, .b = 128},
        .p_gamma_correction = nullptr,
        .dithering_on = false,
    },
    .layer =
    {
        {.coordinate = {.x = 0, .y = 0}, .bg_color = {.byte = {.b = 0, .g = 0, .r = 0, .a = 255}},
         .fade_control = DISPLAY_FADE_CONTROL_NONE, .fade_speed = 0},
        {.coordinate = {.x = 0, .y = 0}, .bg_color = {.byte = {.b = 0, .g = 0, .r = 0, .a = 255}},
         .fade_control = DISPLAY_FADE_CONTROL_NONE, .fade_speed = 0},
    },
    .line_detect_ipl = BSP_IRQ_DISABLED,
    .underflow_1_ipl = BSP_IRQ_DISABLED,
    .underflow_2_ipl = BSP_IRQ_DISABLED,
    .line_detect_irq = FSP_INVALID_VECTOR,
    .underflow_1_irq = FSP_INVALID_VECTOR,
    .underflow_2_irq = FSP_INVALID_VECTOR,
    .p_callback = nullptr,
    .p_context = nullptr,
    .p_extend = &g_display_extend_cfg,
};

bool g_display_started = false;
bool g_backlight_on = false;
uint32_t g_display_front_buffer = 0U;

bool wait_for_display_vsync()
{
    R_GLCDC->SYSCNT.STCLR_b.VPOSCLR = 1U;

    for (uint32_t attempt = 0U; attempt < 100U; ++attempt)
    {
        if (R_GLCDC->SYSCNT.STMON_b.VPOS != 0U)
        {
            R_GLCDC->SYSCNT.STCLR_b.VPOSCLR = 1U;
            return true;
        }
        tk_dly_tsk(1);
    }

    tm_putstring((UB *)"Display VSYNC wait timed out.\n");
    return false;
}

void set_backlight(bsp_io_level_t level)
{
    R_BSP_PinAccessEnable();
    R_IOPORT_PinWrite(g_ioport.p_ctrl, kBacklightPin, level);
    R_BSP_PinAccessDisable();
}

void set_panel_reset(bsp_io_level_t level)
{
    R_BSP_PinAccessEnable();
    R_IOPORT_PinWrite(g_ioport.p_ctrl, DISP_RESET, level);
    R_BSP_PinAccessDisable();
}

inline uint16_t rgb565_pixel(const uint8_t *line, uint32_t x)
{
    const uint32_t pixel_offset = x * 2U;
    return static_cast<uint16_t>(line[pixel_offset]) |
           (static_cast<uint16_t>(line[pixel_offset + 1U]) << 8U);
}

void fill_horizontal_line(uint16_t *framebuffer, uint32_t x0, uint32_t x1, uint32_t y, uint16_t color)
{
    if (y >= kPanelHeight)
    {
        return;
    }
    x1 = std::min(x1, kPanelWidth - 1U);
    for (uint32_t x = std::min(x0, kPanelWidth - 1U); x <= x1; ++x)
    {
        framebuffer[y * kPanelWidth + x] = color;
    }
}

void fill_vertical_line(uint16_t *framebuffer, uint32_t x, uint32_t y0, uint32_t y1, uint16_t color)
{
    if (x >= kPanelWidth)
    {
        return;
    }
    y1 = std::min(y1, kPanelHeight - 1U);
    for (uint32_t y = std::min(y0, kPanelHeight - 1U); y <= y1; ++y)
    {
        framebuffer[y * kPanelWidth + x] = color;
    }
}

void draw_box(uint16_t *framebuffer, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint16_t color)
{
    for (uint32_t line = 0; line < kBoxThickness; ++line)
    {
        fill_horizontal_line(framebuffer, left, right, top + line, color);
        fill_horizontal_line(framebuffer, left, right, bottom >= line ? bottom - line : 0U, color);
        fill_vertical_line(framebuffer, left + line, top, bottom, color);
        fill_vertical_line(framebuffer, right >= line ? right - line : 0U, top, bottom, color);
    }
}

bool change_display_buffer(uint16_t *framebuffer)
{
    for (uint32_t attempt = 0U; attempt < 20U; ++attempt)
    {
        const fsp_err_t error = R_GLCDC_BufferChange(
            &g_display_ctrl, reinterpret_cast<uint8_t *>(framebuffer), DISPLAY_FRAME_LAYER_1);
        if (error == FSP_SUCCESS)
        {
            return true;
        }
        if (error != FSP_ERR_INVALID_UPDATE_TIMING)
        {
            tm_printf((UB *)"Display buffer change failed: %d\n", static_cast<int>(error));
            return false;
        }
        tk_dly_tsk(1);
    }

    tm_putstring((UB *)"Display buffer change timed out.\n");
    return false;
}
}

bool fish_bbox_display_start()
{
    set_backlight(BSP_IO_LEVEL_LOW);
    set_panel_reset(BSP_IO_LEVEL_LOW);
    tk_dly_tsk(10);
    set_panel_reset(BSP_IO_LEVEL_HIGH);
    tk_dly_tsk(20);

    std::memset(&g_display_framebuffers[0][0][0], 0, sizeof(g_display_framebuffers));
    g_display_front_buffer = 0U;

    fsp_err_t error = R_GLCDC_Open(&g_display_ctrl, &g_display_cfg);
    if (error != FSP_SUCCESS)
    {
        tm_printf((UB *)"Display GLCDC open failed: %d\n", static_cast<int>(error));
        return false;
    }
    error = R_GLCDC_Start(&g_display_ctrl);
    if (error != FSP_SUCCESS)
    {
        tm_printf((UB *)"Display GLCDC start failed: %d\n", static_cast<int>(error));
        return false;
    }

    g_display_started = true;
    set_backlight(BSP_IO_LEVEL_HIGH);
    g_backlight_on = true;
    tm_putstring((UB *)"Display started: 1024x600 RGB565 framebuffer in SDRAM.\n");
    return true;
}

void fish_bbox_display_render(const uint8_t *frame,
                              float box_x, float box_y, float box_w, float box_h,
                              bool detected, const FeedingMonitorResult &feeding)
{
    if (!g_display_started || frame == nullptr)
    {
        return;
    }

    if (!wait_for_display_vsync())
    {
        return;
    }

    const uint32_t back_buffer = g_display_front_buffer ^ 1U;
    uint16_t *framebuffer = &g_display_framebuffers[back_buffer][0][0];

    for (uint32_t y = 0; y < kCameraHeight; ++y)
    {
        const uint8_t *src_line = frame + y * kCameraStrideBytes;
        uint16_t *dst_line = framebuffer + (kPanelOffsetY + y) * kPanelWidth + kPanelOffsetX;
        for (uint32_t x = 0; x < kCameraWidth; ++x)
        {
            dst_line[x] = rgb565_pixel(src_line, x);
        }
    }

    if (detected)
    {
        // 学習時と同じ正方形リサイズ後のモデル座標をカメラ画素へ戻す。
        float left_f = box_x * kModelInputSize * (static_cast<float>(kCameraWidth) / kModelInputSize);
        float top_f = box_y * static_cast<float>(kCameraHeight);
        float width_f = box_w * static_cast<float>(kCameraWidth);
        float height_f = box_h * static_cast<float>(kCameraHeight);

        left_f = std::max(0.0f, std::min(left_f, static_cast<float>(kCameraWidth - 1U)));
        top_f = std::max(0.0f, std::min(top_f, static_cast<float>(kCameraHeight - 1U)));
        float right_f = std::max(left_f, std::min(left_f + width_f, static_cast<float>(kCameraWidth - 1U)));
        float bottom_f = std::max(top_f, std::min(top_f + height_f, static_cast<float>(kCameraHeight - 1U)));

        draw_box(framebuffer,
             kPanelOffsetX + static_cast<uint32_t>(left_f),
                 kPanelOffsetY + static_cast<uint32_t>(top_f),
                 kPanelOffsetX + static_cast<uint32_t>(right_f),
                 kPanelOffsetY + static_cast<uint32_t>(bottom_f),
                 kBoxColorDetected);
    }

    if (feeding.food.candidate)
    {
        const uint32_t center_x = kPanelOffsetX + static_cast<uint32_t>(feeding.food.x);
        const uint32_t center_y = kPanelOffsetY + static_cast<uint32_t>(feeding.food.y);
        draw_box(framebuffer, center_x > 5U ? center_x - 5U : 0U,
                 center_y > 5U ? center_y - 5U : 0U,
                 center_x + 5U, center_y + 5U, kFoodColor);
    }

    uint16_t status_color = kStatusMonitoringColor;
    if (feeding.gap_state == FeedingGapState::Alert)
    {
        status_color = kStatusAlertColor;
    }
    else if (feeding.state == FeedingState::FeedingCompleted)
    {
        status_color = kStatusCompletedColor;
    }
    else if (feeding.state == FeedingState::Unknown ||
             feeding.gap_state == FeedingGapState::TimeUnknown)
    {
        status_color = kStatusUnknownColor;
    }
    fill_horizontal_line(framebuffer, 0U, kPanelWidth - 1U, 0U, status_color);
    fill_horizontal_line(framebuffer, 0U, kPanelWidth - 1U, 1U, status_color);
    fill_horizontal_line(framebuffer, 0U, kPanelWidth - 1U, 2U, status_color);

    if (change_display_buffer(framebuffer))
    {
        g_display_front_buffer = back_buffer;
    }

    if (!g_backlight_on)
    {
        set_backlight(BSP_IO_LEVEL_HIGH);
        g_backlight_on = true;
    }
}
