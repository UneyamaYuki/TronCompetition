#ifndef FISH_BBOX_DISPLAY_HPP_
#define FISH_BBOX_DISPLAY_HPP_

#include "feeding_monitor.hpp"

#include <cstdint>

bool fish_bbox_display_start();
// box_*: 学習時と同じ256x256入力空間の正規化bbox [0,1]
void fish_bbox_display_render(const uint8_t *frame,
                              float box_x, float box_y, float box_w, float box_h,
                              bool detected, const FeedingMonitorResult &feeding);

#endif
