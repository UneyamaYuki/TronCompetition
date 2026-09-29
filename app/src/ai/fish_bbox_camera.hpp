#ifndef FISH_BBOX_CAMERA_HPP_
#define FISH_BBOX_CAMERA_HPP_

#include <cstddef>
#include <cstdint>

bool fish_bbox_camera_start();
bool fish_bbox_camera_capture_start();
const uint8_t *fish_bbox_camera_latest_frame();
uint32_t fish_bbox_camera_extra_notify_count();
void fish_bbox_camera_report_status();
bool fish_bbox_camera_prepare_input(uint8_t *output, size_t output_bytes,
                                    float input_scale, int32_t input_zero_point,
                                    uint32_t *frame_sequence);

#endif