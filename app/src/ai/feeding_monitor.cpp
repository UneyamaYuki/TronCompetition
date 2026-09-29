#include "feeding_monitor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr uint16_t kSourceWidth = 640U;
constexpr uint16_t kSourceHeight = 480U;
constexpr uint16_t kScale = 4U;
constexpr uint16_t kMinimumArea = 3U;
constexpr uint16_t kMaximumArea = 180U;
constexpr uint16_t kSceneMotionLimit = 2400U;
constexpr float kFragmentMergeDistance = 12.0f;
constexpr float kMinimumSinkSpeed = 8.0f;
constexpr float kMaximumSinkSpeed = 220.0f;
constexpr float kMaximumHorizontalSpeed = 100.0f;
constexpr float kFishNearDistance = 100.0f;

struct Component
{
    uint16_t area = 0U;
    uint16_t min_x = 0U;
    uint16_t max_x = 0U;
    uint16_t min_y = 0U;
    uint16_t max_y = 0U;
    uint32_t sum_x = 0U;
    uint32_t sum_y = 0U;
    uint32_t intensity = 0U;
    uint16_t warm_pixels = 0U;
};

uint8_t absolute_difference(uint8_t first, uint8_t second)
{
    return first > second ? static_cast<uint8_t>(first - second)
                          : static_cast<uint8_t>(second - first);
}

float squared_distance(float first_x, float first_y, float second_x, float second_y)
{
    const float delta_x = first_x - second_x;
    const float delta_y = first_y - second_y;
    return delta_x * delta_x + delta_y * delta_y;
}

float temporal_association_distance(uint32_t elapsed_ms)
{
    const float seconds = static_cast<float>(elapsed_ms) / 1000.0f;
    const float maximum_displacement = std::sqrt(
        kMaximumSinkSpeed * kMaximumSinkSpeed +
        kMaximumHorizontalSpeed * kMaximumHorizontalSpeed) * seconds;
    return std::max(kFragmentMergeDistance, maximum_displacement);
}
}

FeedingMonitor::FeedingMonitor(const FeedingMonitorConfig &config) : config_(config)
{
}

FoodObservation FeedingMonitor::detect_food(const uint8_t *rgb565, size_t stride_bytes,
                                             uint32_t elapsed_ms, bool *scene_valid)
{
    uint16_t histogram[256]{};
    for (uint16_t y = 0; y < kGridHeight; ++y)
    {
        for (uint16_t x = 0; x < kGridWidth; ++x)
        {
            const size_t offset = static_cast<size_t>(y * kScale) * stride_bytes +
                                  static_cast<size_t>(x * kScale) * 2U;
            const uint16_t pixel = static_cast<uint16_t>(rgb565[offset]) |
                                   (static_cast<uint16_t>(rgb565[offset + 1U]) << 8U);
            const uint8_t red = static_cast<uint8_t>(((pixel >> 11U) & 0x1fU) * 255U / 31U);
            const uint8_t green = static_cast<uint8_t>(((pixel >> 5U) & 0x3fU) * 255U / 63U);
            const uint8_t blue = static_cast<uint8_t>((pixel & 0x1fU) * 255U / 31U);
            const uint16_t index = static_cast<uint16_t>(y * kGridWidth + x);
            current_gray_[index] = static_cast<uint8_t>((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
            const uint8_t minimum_warm = std::min(red, green);
            warm_mask_[index] = minimum_warm > 20U && std::max(red, green) < 210U &&
                                static_cast<int>(minimum_warm) - blue > 15 ? 1U : 0U;
            if (has_previous_frame_)
            {
                ++histogram[absolute_difference(current_gray_[index], previous_gray_[index])];
            }
        }
    }

    FoodObservation best{};
    if (!has_previous_frame_)
    {
        std::memcpy(previous_gray_, current_gray_, sizeof(previous_gray_));
        has_previous_frame_ = true;
        *scene_valid = true;
        return best;
    }

    uint32_t upper_pixels = 0U;
    uint8_t threshold = 255U;
    for (int value = 255; value >= 0; --value)
    {
        upper_pixels += histogram[value];
        if (upper_pixels >= kGridPixels / 50U)
        {
            threshold = static_cast<uint8_t>(value);
            break;
        }
    }
    threshold = std::min<uint8_t>(std::max<uint8_t>(threshold, 10U), 64U);

    uint16_t moving_pixels = 0U;
    for (uint16_t y = 0; y < kGridHeight; ++y)
    {
        for (uint16_t x = 0; x < kGridWidth; ++x)
        {
            const uint16_t index = static_cast<uint16_t>(y * kGridWidth + x);
            const uint8_t difference = absolute_difference(current_gray_[index], previous_gray_[index]);
            motion_mask_[index] = difference >= threshold ? difference : 0U;
            moving_pixels += motion_mask_[index] != 0U;
        }
    }
    for (uint16_t y = 1U; y + 1U < kGridHeight; ++y)
    {
        for (uint16_t x = 1U; x + 1U < kGridWidth; ++x)
        {
            const uint16_t index = static_cast<uint16_t>(y * kGridWidth + x);
            uint8_t neighbors = 0U;
            for (int offset_y = -1; offset_y <= 1; ++offset_y)
            {
                for (int offset_x = -1; offset_x <= 1; ++offset_x)
                {
                    neighbors += motion_mask_[(y + offset_y) * kGridWidth + x + offset_x] != 0U;
                }
            }
            visited_[index] = neighbors >= 3U ? motion_mask_[index] : 0U;
        }
    }
    std::memcpy(motion_mask_, visited_, sizeof(motion_mask_));
    std::memcpy(previous_gray_, current_gray_, sizeof(previous_gray_));
    *scene_valid = moving_pixels < kSceneMotionLimit;
    if (!*scene_valid)
    {
        return best;
    }

    std::memset(visited_, 0, sizeof(visited_));
    Component components[32]{};
    uint8_t component_count = 0U;
    for (uint16_t seed = 0; seed < kGridPixels && component_count < 32U; ++seed)
    {
        if (motion_mask_[seed] == 0U || visited_[seed] != 0U)
        {
            continue;
        }
        Component component{};
        component.min_x = component.max_x = static_cast<uint16_t>(seed % kGridWidth);
        component.min_y = component.max_y = static_cast<uint16_t>(seed / kGridWidth);
        uint16_t head = 0U;
        uint16_t tail = 0U;
        component_queue_[tail++] = seed;
        visited_[seed] = 1U;
        while (head < tail)
        {
            const uint16_t index = component_queue_[head++];
            const uint16_t x = static_cast<uint16_t>(index % kGridWidth);
            const uint16_t y = static_cast<uint16_t>(index / kGridWidth);
            ++component.area;
            component.sum_x += x;
            component.sum_y += y;
            component.intensity += motion_mask_[index];
            component.warm_pixels += warm_mask_[index];
            component.min_x = std::min(component.min_x, x);
            component.max_x = std::max(component.max_x, x);
            component.min_y = std::min(component.min_y, y);
            component.max_y = std::max(component.max_y, y);
            for (int offset_y = -1; offset_y <= 1; ++offset_y)
            {
                for (int offset_x = -1; offset_x <= 1; ++offset_x)
                {
                    const int neighbor_x = static_cast<int>(x) + offset_x;
                    const int neighbor_y = static_cast<int>(y) + offset_y;
                    if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= kGridWidth || neighbor_y >= kGridHeight)
                    {
                        continue;
                    }
                    const uint16_t neighbor = static_cast<uint16_t>(neighbor_y * kGridWidth + neighbor_x);
                    if (motion_mask_[neighbor] != 0U && visited_[neighbor] == 0U)
                    {
                        visited_[neighbor] = 1U;
                        component_queue_[tail++] = neighbor;
                    }
                }
            }
        }
        if (component.area >= kMinimumArea && component.area <= kMaximumArea)
        {
            components[component_count++] = component;
        }
    }

    float best_score = -1.0e30f;
    for (uint8_t first = 0U; first < component_count; ++first)
    {
        if (components[first].area == 0U)
        {
            continue;
        }
        for (uint8_t second = static_cast<uint8_t>(first + 1U); second < component_count; ++second)
        {
            if (components[second].area == 0U)
            {
                continue;
            }
            const float first_x = static_cast<float>(components[first].sum_x) / components[first].area;
            const float first_y = static_cast<float>(components[first].sum_y) / components[first].area;
            const float second_x = static_cast<float>(components[second].sum_x) / components[second].area;
            const float second_y = static_cast<float>(components[second].sum_y) / components[second].area;
            if (squared_distance(first_x, first_y, second_x, second_y) <=
                (kFragmentMergeDistance / kScale) * (kFragmentMergeDistance / kScale))
            {
                components[first].area = static_cast<uint16_t>(components[first].area + components[second].area);
                components[first].sum_x += components[second].sum_x;
                components[first].sum_y += components[second].sum_y;
                components[first].intensity += components[second].intensity;
                components[first].warm_pixels = static_cast<uint16_t>(components[first].warm_pixels +
                                                                        components[second].warm_pixels);
                components[second].area = 0U;
            }
        }
    }
    for (uint8_t index = 0; index < component_count; ++index)
    {
        const Component &component = components[index];
        if (component.area == 0U)
        {
            continue;
        }
        if ((!has_track_ || !food_confirmed_) && component.warm_pixels == 0U)
        {
            continue;
        }
        const float center_x = static_cast<float>(component.sum_x) / component.area * kScale;
        const float center_y = static_cast<float>(component.sum_y) / component.area * kScale;
        const float warm_bonus = static_cast<float>(component.warm_pixels) * 200.0f;
        const float score = has_track_
            ? warm_bonus - squared_distance(center_x, center_y, tracked_food_.x, tracked_food_.y)
            : warm_bonus + static_cast<float>(component.intensity) / component.area;
        const float association_distance = has_track_
            ? temporal_association_distance(elapsed_ms)
            : kFragmentMergeDistance;
        const bool associated = !has_track_ ||
            squared_distance(center_x, center_y, tracked_food_.x, tracked_food_.y) <=
                association_distance * association_distance;
        if (associated && score > best_score)
        {
            best_score = score;
            best.candidate = true;
            best.x = center_x;
            best.y = center_y;
            best.area = static_cast<uint16_t>(component.area * kScale * kScale);
        }
    }

    if (!best.candidate && food_confirmed_ && has_track_)
    {
        const int center_x = static_cast<int>(tracked_food_.x / kScale);
        const int center_y = static_cast<int>(tracked_food_.y / kScale);
        uint16_t warm_pixels = 0U;
        for (int y = std::max(0, center_y - 2); y <= std::min<int>(kGridHeight - 1U, center_y + 2); ++y)
        {
            for (int x = std::max(0, center_x - 2); x <= std::min<int>(kGridWidth - 1U, center_x + 2); ++x)
            {
                warm_pixels += warm_mask_[y * kGridWidth + x];
            }
        }
        if (warm_pixels >= 2U)
        {
            best = tracked_food_;
            best.candidate = true;
            best.sinking = false;
            best.vertical_speed = 0.0f;
        }
    }

    if (best.candidate && has_track_ && elapsed_ms > 0U)
    {
        const float seconds = static_cast<float>(elapsed_ms) / 1000.0f;
        best.vertical_speed = (best.y - tracked_food_.y) / seconds;
        const float horizontal_speed = std::fabs(best.x - tracked_food_.x) / seconds;
        best.sinking = best.vertical_speed >= kMinimumSinkSpeed &&
                       best.vertical_speed <= kMaximumSinkSpeed &&
                       horizontal_speed <= kMaximumHorizontalSpeed;
    }
    return best;
}

FeedingMonitorResult FeedingMonitor::process(const uint8_t *rgb565, size_t stride_bytes,
                                             uint32_t monotonic_ms, const FishObservation &fish)
{
    bool scene_valid = false;
    const uint32_t elapsed_ms = has_previous_frame_ ? monotonic_ms - previous_frame_ms_ : 0U;
    FoodObservation food = detect_food(rgb565, stride_bytes, elapsed_ms, &scene_valid);
    previous_frame_ms_ = monotonic_ms;
    bool completed = false;

    if (!scene_valid || (!fish.valid && (food_confirmed_ || state_ == FeedingState::EatingCandidate)))
    {
        state_ = FeedingState::Unknown;
    }
    else if (food.candidate)
    {
        disappearance_start_ms_ = 0U;
        if (has_track_ && !food_confirmed_)
        {
            consecutive_sink_steps_ = food.sinking
                ? static_cast<uint8_t>(consecutive_sink_steps_ + 1U)
                : 0U;
        }
        else if (!has_track_)
        {
            consecutive_sink_steps_ = 0U;
        }
        has_track_ = true;
        tracked_food_ = food;
        last_track_ms_ = monotonic_ms;
        if (food_confirmed_ || consecutive_sink_steps_ >= 2U)
        {
            food_confirmed_ = true;
            state_ = FeedingState::FoodConfirmed;
        }
        else if (consecutive_sink_steps_ > 0U)
        {
            state_ = FeedingState::Tracking;
        }
        else
        {
            state_ = FeedingState::FoodCandidate;
        }

        if (food_confirmed_ && fish.valid)
        {
            const float fish_x = (fish.x + fish.width * 0.5f) * kSourceWidth;
            const float fish_y = (fish.y + fish.height * 0.5f) * kSourceHeight;
            const float distance = std::sqrt(squared_distance(food.x, food.y, fish_x, fish_y));
            if (previous_fish_distance_ > 0.0f && distance + 4.0f < previous_fish_distance_)
            {
                fish_was_approaching_ = true;
                state_ = FeedingState::FishApproaching;
            }
            previous_fish_distance_ = distance;
        }
    }
    else if (food_confirmed_ && fish.valid && fish_was_approaching_ &&
             previous_fish_distance_ <= kFishNearDistance)
    {
        if (disappearance_start_ms_ == 0U)
        {
            disappearance_start_ms_ = monotonic_ms;
            state_ = FeedingState::EatingCandidate;
        }
        else if (monotonic_ms - disappearance_start_ms_ >= config_.disappearance_confirm_ms)
        {
            state_ = FeedingState::FeedingCompleted;
            completed = true;
            last_feeding_monotonic_ms_ = monotonic_ms;
            has_last_feeding_monotonic_ = true;
            if (wall_clock_valid_)
            {
                last_feeding_unix_ms_ = current_unix_time(monotonic_ms);
                has_last_feeding_time_ = true;
            }
            has_track_ = false;
            food_confirmed_ = false;
            consecutive_sink_steps_ = 0U;
            fish_was_approaching_ = false;
        }
    }
    else if (has_track_ && monotonic_ms - last_track_ms_ > config_.tracking_timeout_ms)
    {
        state_ = FeedingState::Unknown;
        has_track_ = false;
        food_confirmed_ = false;
        consecutive_sink_steps_ = 0U;
        fish_was_approaching_ = false;
    }
    else if (!has_track_)
    {
        state_ = FeedingState::Idle;
    }

    FeedingMonitorResult monitor_result = result(monotonic_ms);
    monitor_result.food = food;
    monitor_result.feeding_completed = completed;
    return monitor_result;
}

void FeedingMonitor::set_wall_clock(uint64_t unix_time_ms, uint32_t monotonic_ms)
{
    wall_clock_base_unix_ms_ = unix_time_ms;
    wall_clock_base_ms_ = monotonic_ms;
    wall_clock_valid_ = true;
}

void FeedingMonitor::restore_last_feeding_time(uint64_t unix_time_ms)
{
    last_feeding_unix_ms_ = unix_time_ms;
    has_last_feeding_time_ = true;
}

uint64_t FeedingMonitor::last_feeding_time() const
{
    return has_last_feeding_time_ ? last_feeding_unix_ms_ : 0U;
}

uint64_t FeedingMonitor::current_unix_time(uint32_t monotonic_ms) const
{
    return wall_clock_base_unix_ms_ + static_cast<uint32_t>(monotonic_ms - wall_clock_base_ms_);
}

FeedingGapState FeedingMonitor::gap_state(uint32_t monotonic_ms, uint32_t *elapsed_seconds) const
{
    *elapsed_seconds = 0U;
    if (wall_clock_valid_ && has_last_feeding_time_)
    {
        const uint64_t now = current_unix_time(monotonic_ms);
        if (now < last_feeding_unix_ms_)
        {
            return FeedingGapState::TimeUnknown;
        }
        const uint64_t elapsed_ms = now - last_feeding_unix_ms_;
        *elapsed_seconds = static_cast<uint32_t>(elapsed_ms / 1000U);
        return elapsed_ms >= config_.alert_after_ms ? FeedingGapState::Alert : FeedingGapState::Monitoring;
    }

    if (!has_last_feeding_monotonic_)
    {
        return FeedingGapState::TimeUnknown;
    }
    const uint32_t elapsed_ms = monotonic_ms - last_feeding_monotonic_ms_;
    *elapsed_seconds = static_cast<uint32_t>(elapsed_ms / 1000U);
    return elapsed_ms >= config_.alert_after_ms ? FeedingGapState::Alert : FeedingGapState::Monitoring;
}

FeedingMonitorResult FeedingMonitor::result(uint32_t monotonic_ms) const
{
    FeedingMonitorResult monitor_result{};
    monitor_result.state = state_;
    monitor_result.gap_state = gap_state(monotonic_ms, &monitor_result.seconds_since_feeding);
    monitor_result.food = tracked_food_;
    return monitor_result;
}

const char *feeding_state_name(FeedingState state)
{
    static const char *const names[] = {"IDLE", "FOOD_CANDIDATE", "TRACKING", "FOOD_CONFIRMED",
                                        "FISH_APPROACHING", "EATING_CANDIDATE", "FEEDING_COMPLETED", "UNKNOWN"};
    return names[static_cast<uint8_t>(state)];
}

const char *feeding_gap_state_name(FeedingGapState state)
{
    static const char *const names[] = {"TIME_UNKNOWN", "MONITORING", "ALERT"};
    return names[static_cast<uint8_t>(state)];
}