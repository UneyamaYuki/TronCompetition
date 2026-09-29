#ifndef FEEDING_MONITOR_HPP_
#define FEEDING_MONITOR_HPP_

#include <cstddef>
#include <cstdint>

enum class FeedingState : uint8_t
{
    Idle,
    FoodCandidate,
    Tracking,
    FoodConfirmed,
    FishApproaching,
    EatingCandidate,
    FeedingCompleted,
    Unknown,
};

enum class FeedingGapState : uint8_t
{
    TimeUnknown,
    Monitoring,
    Alert,
};

struct FishObservation
{
    bool valid;
    float x;
    float y;
    float width;
    float height;
};

struct FoodObservation
{
    bool candidate;
    bool sinking;
    float x;
    float y;
    float vertical_speed;
    uint16_t area;
};

struct FeedingMonitorResult
{
    FeedingState state;
    FeedingGapState gap_state;
    FoodObservation food;
    uint32_t seconds_since_feeding;
    bool feeding_completed;
};

struct FeedingMonitorConfig
{
    uint32_t alert_after_ms = 3U * 24U * 60U * 60U * 1000U;
    uint32_t disappearance_confirm_ms = 1200U;
    uint32_t tracking_timeout_ms = 1600U;
};

class FeedingMonitor
{
public:
    explicit FeedingMonitor(const FeedingMonitorConfig &config = FeedingMonitorConfig{});

    FeedingMonitorResult process(const uint8_t *rgb565, size_t stride_bytes,
                                 uint32_t monotonic_ms, const FishObservation &fish);
    void set_wall_clock(uint64_t unix_time_ms, uint32_t monotonic_ms);
    void restore_last_feeding_time(uint64_t unix_time_ms);
    uint64_t last_feeding_time() const;
    FeedingMonitorResult result(uint32_t monotonic_ms) const;

private:
    static constexpr uint16_t kGridWidth = 160U;
    static constexpr uint16_t kGridHeight = 120U;
    static constexpr uint16_t kGridPixels = kGridWidth * kGridHeight;

    FeedingMonitorConfig config_;
    uint8_t previous_gray_[kGridPixels]{};
    uint8_t current_gray_[kGridPixels]{};
    uint8_t motion_mask_[kGridPixels]{};
    uint8_t warm_mask_[kGridPixels]{};
    uint8_t visited_[kGridPixels]{};
    uint16_t component_queue_[kGridPixels]{};
    bool has_previous_frame_ = false;
    bool has_track_ = false;
    bool food_confirmed_ = false;
    bool fish_was_approaching_ = false;
    bool wall_clock_valid_ = false;
    bool has_last_feeding_time_ = false;
    bool has_last_feeding_monotonic_ = false;
    FeedingState state_ = FeedingState::Idle;
    FoodObservation tracked_food_{};
    float previous_fish_distance_ = 0.0f;
    uint8_t consecutive_sink_steps_ = 0U;
    uint32_t previous_frame_ms_ = 0U;
    uint32_t last_track_ms_ = 0U;
    uint32_t disappearance_start_ms_ = 0U;
    uint32_t last_feeding_monotonic_ms_ = 0U;
    uint32_t wall_clock_base_ms_ = 0U;
    uint64_t wall_clock_base_unix_ms_ = 0U;
    uint64_t last_feeding_unix_ms_ = 0U;

    FoodObservation detect_food(const uint8_t *rgb565, size_t stride_bytes,
                                uint32_t elapsed_ms, bool *scene_valid);
    FeedingGapState gap_state(uint32_t monotonic_ms, uint32_t *elapsed_seconds) const;
    uint64_t current_unix_time(uint32_t monotonic_ms) const;
};

const char *feeding_state_name(FeedingState state);
const char *feeding_gap_state_name(FeedingGapState state);

#endif