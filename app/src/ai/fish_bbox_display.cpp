#include "fish_bbox_display.hpp"

#include "hal_data.h"
#include "r_glcdc.h"

extern "C"
{
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr uint32_t kPanelWidth = 1024U;
constexpr uint32_t kPanelHeight = 600U;
constexpr uint32_t kCameraWidth = 640U;
constexpr uint32_t kCameraHeight = 480U;
constexpr uint32_t kCameraStrideBytes = 1280U;
constexpr uint32_t kPanelOffsetX = (kPanelWidth - kCameraWidth) / 2U;
constexpr uint32_t kPanelOffsetY = (kPanelHeight - kCameraHeight) / 2U;
constexpr float kModelInputSize = 256.0f;
constexpr float kLetterboxOffsetY = 32.0f;
constexpr float kLetterboxContentHeight = 192.0f;
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
    .clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_9,
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
            .hsize = kCameraWidth,
            .vsize = kCameraHeight,
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
        {.coordinate = {.x = static_cast<int16_t>(kPanelOffsetX), .y = static_cast<int16_t>(kPanelOffsetY)},
         .bg_color = {.byte = {.b = 0, .g = 0, .r = 0, .a = 255}},
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
uint32_t g_display_render_count = 0U;
uint32_t g_display_underflow_count = 0U;
bool g_display_registers_reported = false;

bool wait_for_display_update()
{
    for (uint32_t attempt = 0U; attempt < 100U; ++attempt)
    {
        if (R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].VEN_b.PVEN == 0U &&
            R_GLCDC->BG.EN_b.VEN == 0U)
        {
            return true;
        }
        tk_dly_tsk(1);
    }

    tm_printf((UB *)"Display update wait timed out: pven=%u bven=%u front=%u\n",
              static_cast<unsigned int>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].VEN_b.PVEN),
              static_cast<unsigned int>(R_GLCDC->BG.EN_b.VEN),
              static_cast<unsigned int>(g_display_front_buffer));
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
    if (y >= kPanelHeight || x0 >= kPanelWidth || x0 > x1)
    {
        return;
    }
    x1 = std::min(x1, kPanelWidth - 1U);
    for (uint32_t x = x0; x <= x1; ++x)
    {
        framebuffer[y * kPanelWidth + x] = color;
    }
}

void fill_vertical_line(uint16_t *framebuffer, uint32_t x, uint32_t y0, uint32_t y1, uint16_t color)
{
    if (x >= kPanelWidth || y0 >= kPanelHeight || y0 > y1)
    {
        return;
    }
    y1 = std::min(y1, kPanelHeight - 1U);
    for (uint32_t y = y0; y <= y1; ++y)
    {
        framebuffer[y * kPanelWidth + x] = color;
    }
}

void draw_box(uint16_t *framebuffer, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint16_t color)
{
    if (left >= kPanelWidth || top >= kPanelHeight || left > right || top > bottom)
    {
        return;
    }
    right = std::min(right, kPanelWidth - 1U);
    bottom = std::min(bottom, kPanelHeight - 1U);
    for (uint32_t line = 0; line < kBoxThickness; ++line)
    {
        fill_horizontal_line(framebuffer, left, right, top + line, color);
        fill_horizontal_line(framebuffer, left, right, bottom >= line ? bottom - line : 0U, color);
        fill_vertical_line(framebuffer, left + line, top, bottom, color);
        fill_vertical_line(framebuffer, right >= line ? right - line : 0U, top, bottom, color);
    }
}

void report_display_frame_diagnostics(const uint16_t *framebuffer, uint32_t back_buffer)
{
    uint32_t yellow_samples = 0U;
    uint32_t sample_count = 0U;
    for (uint32_t y = 0U; y < kCameraHeight; y += 8U)
    {
        for (uint32_t x = 0U; x < kCameraWidth; x += 8U)
        {
            yellow_samples += framebuffer[y * kPanelWidth + x] == kFoodColor ? 1U : 0U;
            ++sample_count;
        }
    }
    if (yellow_samples * 10U >= sample_count * 9U)
    {
        tm_printf((UB *)"Display back buffer mostly yellow: samples=%u/%u back=%u\n",
                  static_cast<unsigned int>(yellow_samples),
                  static_cast<unsigned int>(sample_count),
                  static_cast<unsigned int>(back_buffer));
    }

    if (R_GLCDC->SYSCNT.STMON_b.L1UNDF != 0U)
    {
        ++g_display_underflow_count;
        if (g_display_underflow_count <= 3U || (g_display_underflow_count % 5U) == 0U)
        {
            tm_printf((UB *)"Display layer 1 underflow: count=%u back=%u front=%u\n",
                      static_cast<unsigned int>(g_display_underflow_count),
                      static_cast<unsigned int>(back_buffer),
                      static_cast<unsigned int>(g_display_front_buffer));
        }
        if (!g_display_registers_reported)
        {
            tm_printf((UB *)"GLCDC regs: STMON=%08lx GRMON=%08lx FLM1=%08lx FLM2=%08lx FLM3=%08lx FLM5=%08lx FLM6=%08lx AB1=%08lx BG_EN=%08lx BG_PERI=%08lx\n",
                      static_cast<unsigned long>(R_GLCDC->SYSCNT.STMON),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].MON),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].FLM1),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].FLM2),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].FLM3),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].FLM5),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].FLM6),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].AB1),
                      static_cast<unsigned long>(R_GLCDC->BG.EN),
                      static_cast<unsigned long>(R_GLCDC->BG.PERI));
            g_display_registers_reported = true;
        }
        const uint32_t status_before_clear = R_GLCDC->SYSCNT.STMON;
        R_GLCDC->SYSCNT.STCLR_b.L1UNDFCLR = 1U;
        const uint32_t status_after_clear = R_GLCDC->SYSCNT.STMON;
        if (g_display_underflow_count <= 3U)
        {
            tm_printf((UB *)"Display underflow clear: before=%08lx after=%08lx grmon=%08lx\n",
                      static_cast<unsigned long>(status_before_clear),
                      static_cast<unsigned long>(status_after_clear),
                      static_cast<unsigned long>(R_GLCDC->GR[DISPLAY_FRAME_LAYER_1].MON));
        }
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
            if (!wait_for_display_update())
            {
                tm_putstring((UB *)"Display buffer change accepted but not latched yet.\n");
            }
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
    g_display_underflow_count = 0U;
    g_display_registers_reported = false;

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

    R_GLCDC->SYSCNT.STCLR = 0x07U;

    g_display_started = true;
    g_display_render_count = 0U;
    set_backlight(BSP_IO_LEVEL_HIGH);
    g_backlight_on = true;
    tm_putstring((UB *)"Display started: 1024x600 RGB565 framebuffer in SDRAM, panel clock DIV9.\n");
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

    if (!wait_for_display_update())
    {
        return;
    }

    const uint32_t back_buffer = g_display_front_buffer ^ 1U;
    uint16_t *framebuffer = &g_display_framebuffers[back_buffer][0][0];
    uint32_t food_color_pixels = 0U;

    for (uint32_t y = 0; y < kCameraHeight; ++y)
    {
        const uint8_t *src_line = frame + y * kCameraStrideBytes;
        uint16_t *dst_line = framebuffer + y * kPanelWidth;
        for (uint32_t x = 0; x < kCameraWidth; ++x)
        {
            const uint16_t pixel = rgb565_pixel(src_line, x);
            dst_line[x] = pixel;
            food_color_pixels += pixel == kFoodColor ? 1U : 0U;
        }
    }

    if (food_color_pixels >= (kCameraWidth * kCameraHeight * 9U) / 10U)
    {
        tm_printf((UB *)"Display source frame mostly yellow: pixels=%u/%u\n",
                  static_cast<unsigned int>(food_color_pixels),
                  static_cast<unsigned int>(kCameraWidth * kCameraHeight));
    }

    if (detected && std::isfinite(box_x) && std::isfinite(box_y) &&
        std::isfinite(box_w) && std::isfinite(box_h))
    {
        float left_f = box_x * static_cast<float>(kCameraWidth);
        float top_f = (box_y * kModelInputSize - kLetterboxOffsetY) *
                  (static_cast<float>(kCameraHeight) / kLetterboxContentHeight);
        float width_f = box_w * static_cast<float>(kCameraWidth);
        float height_f = box_h * kModelInputSize *
                 (static_cast<float>(kCameraHeight) / kLetterboxContentHeight);

        left_f = std::max(0.0f, std::min(left_f, static_cast<float>(kCameraWidth - 1U)));
        top_f = std::max(0.0f, std::min(top_f, static_cast<float>(kCameraHeight - 1U)));
        float right_f = std::max(left_f, std::min(left_f + width_f, static_cast<float>(kCameraWidth - 1U)));
        float bottom_f = std::max(top_f, std::min(top_f + height_f, static_cast<float>(kCameraHeight - 1U)));

        draw_box(framebuffer,
                 static_cast<uint32_t>(left_f),
                 static_cast<uint32_t>(top_f),
                 static_cast<uint32_t>(right_f),
                 static_cast<uint32_t>(bottom_f),
                 kBoxColorDetected);
    }
    else if (detected)
    {
        tm_putstring((UB *)"Display detected box rejected: non-finite model output.\n");
    }

    if (feeding.food.candidate)
    {
        if (std::isfinite(feeding.food.x) && std::isfinite(feeding.food.y))
        {
            const float food_x = std::max(0.0f, std::min(feeding.food.x, static_cast<float>(kCameraWidth - 1U)));
            const float food_y = std::max(0.0f, std::min(feeding.food.y, static_cast<float>(kCameraHeight - 1U)));
            const uint32_t center_x = static_cast<uint32_t>(food_x);
            const uint32_t center_y = static_cast<uint32_t>(food_y);
            draw_box(framebuffer, center_x > 5U ? center_x - 5U : 0U,
                     center_y > 5U ? center_y - 5U : 0U,
                     center_x + 5U, center_y + 5U, kFoodColor);
        }
        else
        {
            tm_putstring((UB *)"Display food marker rejected: non-finite position.\n");
        }
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
    fill_horizontal_line(framebuffer, 0U, kCameraWidth - 1U, 0U, status_color);
    fill_horizontal_line(framebuffer, 0U, kCameraWidth - 1U, 1U, status_color);
    fill_horizontal_line(framebuffer, 0U, kCameraWidth - 1U, 2U, status_color);
    report_display_frame_diagnostics(framebuffer, back_buffer);

    if (change_display_buffer(framebuffer))
    {
        g_display_front_buffer = back_buffer;
        ++g_display_render_count;
        if ((g_display_render_count % 5U) == 0U)
        {
            tm_printf((UB *)"Display render: count=%u front=%u\n",
                      static_cast<unsigned int>(g_display_render_count),
                      static_cast<unsigned int>(g_display_front_buffer));
        }
    }

    if (!g_backlight_on)
    {
        set_backlight(BSP_IO_LEVEL_HIGH);
        g_backlight_on = true;
    }
}
