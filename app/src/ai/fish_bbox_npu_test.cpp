#include "fish_bbox_npu_test.hpp"

#include "ai/model/fish_bbox_model_data.hpp"
#include "ai/fish_bbox_camera.hpp"
#include "ai/fish_bbox_display.hpp"
#include "ai/feeding_monitor.hpp"
#include "ai/ospi_model_storage.hpp"
#include "hal_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "ethosu_driver.h"
#include <algorithm>
#include <cstdint>
extern "C"
{
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
}

namespace
{
constexpr size_t kTensorArenaSize = 1536U * 1024U;
constexpr size_t kModelCopyCapacity = 7U * 1024U * 1024U;
constexpr size_t kModelInputBytes = 256U * 256U * 3U;
constexpr size_t kYoloCandidateCount = 4032U;
constexpr size_t kYoloValuesPerCandidate = 6U;
constexpr size_t kModelOutputBytes = kYoloCandidateCount * kYoloValuesPerCandidate;
constexpr int kInferenceBatchSize = 5;
constexpr RELTIM kInferencePeriodMs = 200;
constexpr float kDetectionConfidenceThreshold = 0.5f;
constexpr float kNmsIouThreshold = 0.45f;
constexpr size_t kEthosuSemaphoreCount = 3U;
constexpr size_t kCameraStrideBytes = 1280U;
constexpr float kModelLetterboxOffsetY = 32.0f / 256.0f;
constexpr float kModelLetterboxContentScaleY = 256.0f / 192.0f;

struct TKernelSemaphore
{
    ID id;
    bool allocated;
};

struct Detection
{
    float left;
    float top;
    float width;
    float height;
    float score;
};

alignas(64) uint8_t g_tensor_arena[kTensorArenaSize]
    __attribute__((section(".sdram_nocache")));
alignas(64) uint8_t g_model_sdram_copy[kModelCopyCapacity]
    __attribute__((section(".sdram_nocache")));
TKernelSemaphore g_ethosu_semaphores[kEthosuSemaphoreCount]{};
FeedingMonitor g_feeding_monitor{};
Detection g_detections[kYoloCandidateCount]{};
bool g_nms_suppressed[kYoloCandidateCount]{};
uint32_t g_ethosu_semaphore_take_trace_count = 0U;
uint32_t g_ethosu_mutex_lock_trace_count = 0U;

TKernelSemaphore *get_ethosu_semaphore(void *handle)
{
    for (size_t index = 0; index < kEthosuSemaphoreCount; ++index)
    {
        if (handle == &g_ethosu_semaphores[index] && g_ethosu_semaphores[index].allocated)
        {
            return &g_ethosu_semaphores[index];
        }
    }
    return nullptr;
}

void *create_ethosu_semaphore(int initial_count)
{
    for (size_t index = 0; index < kEthosuSemaphoreCount; ++index)
    {
        TKernelSemaphore &semaphore = g_ethosu_semaphores[index];
        if (!semaphore.allocated)
        {
            T_CSEM config{};
            config.sematr = TA_TFIFO;
            config.isemcnt = initial_count;
            config.maxsem = 1;
            const ID id = tk_cre_sem(&config);
            if (id <= 0)
            {
                return nullptr;
            }
            semaphore.id = id;
            semaphore.allocated = true;
            return &semaphore;
        }
    }
    return nullptr;
}

void destroy_ethosu_semaphore(void *handle)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(handle);
    if (semaphore != nullptr)
    {
        tk_del_sem(semaphore->id);
        semaphore->allocated = false;
    }
}

bool add_model_operators(tflite::MicroMutableOpResolver<9> &resolver)
{
    return resolver.AddAdd() == kTfLiteOk &&
           resolver.AddConcatenation() == kTfLiteOk &&
           resolver.AddConv2D() == kTfLiteOk &&
           resolver.AddDepthwiseConv2D() == kTfLiteOk &&
           resolver.AddFullyConnected() == kTfLiteOk &&
           resolver.AddLogistic() == kTfLiteOk &&
           resolver.AddMean() == kTfLiteOk &&
           resolver.AddMul() == kTfLiteOk &&
           resolver.AddEthosU() == kTfLiteOk;
}

float detection_iou(const Detection &left, const Detection &right)
{
    const float intersection_left = std::max(left.left, right.left);
    const float intersection_top = std::max(left.top, right.top);
    const float intersection_right = std::min(left.left + left.width, right.left + right.width);
    const float intersection_bottom = std::min(left.top + left.height, right.top + right.height);
    const float intersection_width = std::max(0.0f, intersection_right - intersection_left);
    const float intersection_height = std::max(0.0f, intersection_bottom - intersection_top);
    const float intersection = intersection_width * intersection_height;
    const float left_area = left.width * left.height;
    const float right_area = right.width * right.height;
    return intersection / std::max(left_area + right_area - intersection, 1.0e-6f);
}

bool select_center_detection(const TfLiteTensor *output, Detection *selected)
{
    if (output == nullptr || selected == nullptr || output->data.uint8 == nullptr ||
        output->params.scale <= 0.0f)
    {
        return false;
    }

    size_t detection_count = 0U;
    for (size_t candidate = 0U; candidate < kYoloCandidateCount; ++candidate)
    {
        const uint8_t *values = output->data.uint8 + candidate * kYoloValuesPerCandidate;
        const float objectness = std::max(
            0.0f, std::min(1.0f,
                           (static_cast<float>(values[4]) - output->params.zero_point) *
                               output->params.scale));
        const float class_score = std::max(
            0.0f, std::min(1.0f,
                           (static_cast<float>(values[5]) - output->params.zero_point) *
                               output->params.scale));
        const float score = objectness * class_score;
        const float width = (static_cast<float>(values[2]) - output->params.zero_point) *
                            output->params.scale;
        const float height = (static_cast<float>(values[3]) - output->params.zero_point) *
                             output->params.scale;
        if (score < kDetectionConfidenceThreshold || width <= 0.0f || height <= 0.0f)
        {
            continue;
        }

        const float center_x = (static_cast<float>(values[0]) - output->params.zero_point) *
                               output->params.scale;
        const float center_y = (static_cast<float>(values[1]) - output->params.zero_point) *
                               output->params.scale;
        Detection &detection = g_detections[detection_count++];
        detection.left = std::max(0.0f, std::min(1.0f, center_x - width * 0.5f));
        detection.top = std::max(0.0f, std::min(1.0f, center_y - height * 0.5f));
        const float right = std::max(detection.left,
                                     std::min(1.0f, center_x + width * 0.5f));
        const float bottom = std::max(detection.top,
                                      std::min(1.0f, center_y + height * 0.5f));
        detection.width = right - detection.left;
        detection.height = bottom - detection.top;
        detection.score = score;
        if (detection.width <= 0.0f || detection.height <= 0.0f)
        {
            --detection_count;
        }
    }

    if (detection_count == 0U)
    {
        return false;
    }

    std::sort(g_detections, g_detections + detection_count,
              [](const Detection &left, const Detection &right) {
                  return left.score > right.score;
              });
    std::fill(g_nms_suppressed, g_nms_suppressed + detection_count, false);

    bool found = false;
    float best_center_distance = 2.0f;
    for (size_t index = 0U; index < detection_count; ++index)
    {
        if (g_nms_suppressed[index])
        {
            continue;
        }

        const Detection &detection = g_detections[index];
        const float center_x = detection.left + detection.width * 0.5f;
        const float center_y = detection.top + detection.height * 0.5f;
        const float center_distance = (center_x - 0.5f) * (center_x - 0.5f) +
                                      (center_y - 0.5f) * (center_y - 0.5f);
        if (!found || center_distance < best_center_distance)
        {
            *selected = detection;
            best_center_distance = center_distance;
            found = true;
        }

        for (size_t other = index + 1U; other < detection_count; ++other)
        {
            if (!g_nms_suppressed[other] &&
                detection_iou(detection, g_detections[other]) > kNmsIouThreshold)
            {
                g_nms_suppressed[other] = true;
            }
        }
    }
    return found;
}

bool tensor_shape_matches(const TfLiteTensor *tensor, int rank, const int *shape)
{
    if (tensor == nullptr || tensor->dims == nullptr || tensor->dims->size != rank)
    {
        return false;
    }

    for (int index = 0; index < rank; ++index)
    {
        if (tensor->dims->data[index] != shape[index])
        {
            return false;
        }
    }
    return true;
}

float camera_y_from_model(float model_y)
{
    return std::max(0.0f, std::min(1.0f,
                                    (model_y - kModelLetterboxOffsetY) *
                                        kModelLetterboxContentScaleY));
}

float camera_height_from_model(float model_height)
{
    return std::max(0.0f, std::min(1.0f, model_height * kModelLetterboxContentScaleY));
}

void wait_for_next_inference(const SYSTIM &cycle_start)
{
    SYSTIM current_time{};
    if (tk_get_otm(&current_time) != E_OK)
    {
        tk_dly_tsk(kInferencePeriodMs);
        return;
    }

    const uint32_t elapsed_ms = current_time.lo - cycle_start.lo;
    if (elapsed_ms < kInferencePeriodMs)
    {
        tk_dly_tsk(kInferencePeriodMs - elapsed_ms);
    }
}

void tflm_debug_log_callback(const char *message)
{
    tm_putstring((UB *)message);
}

bool copy_model_to_sdram(const uint8_t *source, size_t length)
{
    if (source == nullptr || length == 0U || length > kModelCopyCapacity)
    {
        return false;
    }

    volatile const uint8_t *source_bytes = source;
    for (size_t index = 0U; index < length; ++index)
    {
        g_model_sdram_copy[index] = source_bytes[index];
    }

    return g_model_sdram_copy[0] == source_bytes[0] &&
           g_model_sdram_copy[1] == source_bytes[1] &&
           g_model_sdram_copy[2] == source_bytes[2] &&
           g_model_sdram_copy[3] == source_bytes[3] &&
           g_model_sdram_copy[length - 1U] == source_bytes[length - 1U];
}
}

extern "C" void *ethosu_mutex_create(void)
{
    return create_ethosu_semaphore(1);
}

extern "C" void ethosu_inference_begin(struct ethosu_driver *drv, void *user_arg)
{
    (void)user_arg;
    static bool power_hold_acquired = false;
    if (!power_hold_acquired && drv != nullptr && ethosu_request_power(drv) == 0)
    {
        power_hold_acquired = true;
        tm_putstring((UB *)"Ethos power hold acquired.\n");
    }
}

extern "C" void ethosu_mutex_destroy(void *mutex)
{
    destroy_ethosu_semaphore(mutex);
}

extern "C" int ethosu_mutex_lock(void *mutex)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(mutex);
    const uint32_t trace_number = ++g_ethosu_mutex_lock_trace_count;
    const bool trace = trace_number <= 12U;
    if (trace)
    {
        tm_printf((UB *)"Ethos mutex lock start #%u mutex=%08x\n",
                  static_cast<unsigned int>(trace_number),
                  static_cast<unsigned int>(reinterpret_cast<uintptr_t>(mutex)));
    }
    const int result = semaphore != nullptr && tk_wai_sem(semaphore->id, 1, TMO_FEVR) == E_OK ? 0 : -1;
    if (trace)
    {
        tm_printf((UB *)"Ethos mutex lock done #%u result=%d\n",
                  static_cast<unsigned int>(trace_number), result);
    }
    return result;
}

extern "C" int ethosu_mutex_unlock(void *mutex)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(mutex);
    return semaphore != nullptr && tk_sig_sem(semaphore->id, 1) == E_OK ? 0 : -1;
}

extern "C" void *ethosu_semaphore_create(void)
{
    return create_ethosu_semaphore(0);
}

extern "C" void ethosu_semaphore_destroy(void *sem)
{
    destroy_ethosu_semaphore(sem);
}

extern "C" int ethosu_semaphore_take(void *sem, uint64_t timeout)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(sem);
    const bool inference_semaphore = semaphore == &g_ethosu_semaphores[2];
    const TMO wait_time = timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER ?
                              (inference_semaphore ? 2000 : TMO_FEVR) :
                              static_cast<TMO>(timeout);
    const uint32_t trace_number = ++g_ethosu_semaphore_take_trace_count;
    const bool trace = trace_number <= 12U;
    if (trace)
    {
        tm_printf((UB *)"Ethos semaphore take start #%u sem=%08x forever=%u\n",
                  static_cast<unsigned int>(trace_number),
                  static_cast<unsigned int>(reinterpret_cast<uintptr_t>(sem)),
                  static_cast<unsigned int>(timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER));
    }
    const int result = semaphore != nullptr && tk_wai_sem(semaphore->id, 1, wait_time) == E_OK ? 0 : -1;
    if (trace)
    {
        tm_printf((UB *)"Ethos semaphore take done #%u result=%d\n",
                  static_cast<unsigned int>(trace_number), result);
    }
    if (inference_semaphore && result != 0)
    {
        volatile const uint32_t *npu_registers =
            reinterpret_cast<volatile const uint32_t *>(R_NPU_BASE);
        tm_printf((UB *)"Ethos NPU wait timeout: STATUS=%08x CMD=%08x QREAD=%08x QSIZE=%08x\n",
                  static_cast<unsigned int>(npu_registers[1]),
                  static_cast<unsigned int>(npu_registers[2]),
                  static_cast<unsigned int>(npu_registers[6]),
                  static_cast<unsigned int>(npu_registers[8]));
    }
    return result;
}

extern "C" int ethosu_semaphore_give(void *sem)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(sem);
    return semaphore != nullptr && tk_sig_sem(semaphore->id, 1) == E_OK ? 0 : -1;
}

extern "C" void fish_bbox_npu_test_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    tm_putstring((UB *)"Fish model NPU test start.\n");

    rm_ethosu_cfg_t ethosu_cfg = g_rm_ethosu0_cfg;
    ethosu_cfg.privilege_enable = false;
    fsp_err_t err = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &ethosu_cfg);
    if (FSP_SUCCESS != err)
    {
        tm_printf((UB *)"RM_ETHOSU_Open failed: %d\n", (int)err);
        volatile const uint32_t *npu_registers =
            reinterpret_cast<volatile const uint32_t *>(R_NPU_BASE);
        tm_printf((UB *)"NPU ID=%08x STATUS=%08x RESET=%08x PROT=%08x CONFIG=%08x\n",
                  (unsigned int)npu_registers[0],
                  (unsigned int)npu_registers[1],
                  (unsigned int)npu_registers[3],
                  (unsigned int)npu_registers[9],
                  (unsigned int)npu_registers[10]);
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    if (!fish_bbox_model_storage_start())
    {
        tm_putstring((UB *)"Model OSPI initialization failed.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    tm_putstring((UB *)"Model header read start.\n");
    const volatile uint8_t *model_bytes =
        reinterpret_cast<const volatile uint8_t *>(g_fish_bbox_model_data);
    tm_printf((UB *)"Model addr=%08x header=%02x %02x %02x %02x %02x %02x %02x %02x\n",
              static_cast<unsigned int>(reinterpret_cast<uintptr_t>(model_bytes)),
              static_cast<unsigned int>(model_bytes[0]),
              static_cast<unsigned int>(model_bytes[1]),
              static_cast<unsigned int>(model_bytes[2]),
              static_cast<unsigned int>(model_bytes[3]),
              static_cast<unsigned int>(model_bytes[4]),
              static_cast<unsigned int>(model_bytes[5]),
              static_cast<unsigned int>(model_bytes[6]),
              static_cast<unsigned int>(model_bytes[7]));
    tm_printf((UB *)"Copying model to SDRAM: %u bytes\n",
              static_cast<unsigned int>(g_fish_bbox_model_data_len));
    if (!copy_model_to_sdram(g_fish_bbox_model_data, g_fish_bbox_model_data_len))
    {
        tm_putstring((UB *)"Model SDRAM copy failed.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }
    tm_printf((UB *)"Model SDRAM copy ready addr=%08x\n",
              static_cast<unsigned int>(reinterpret_cast<uintptr_t>(g_model_sdram_copy)));
    const tflite::Model *model = tflite::GetModel(g_model_sdram_copy);
    tm_putstring((UB *)"Model pointer acquired.\n");
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION)
    {
        tm_putstring((UB *)"Invalid model or schema version.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    tflite::MicroMutableOpResolver<9> resolver;
    if (!add_model_operators(resolver))
    {
        tm_putstring((UB *)"Failed to register model operators.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    RegisterDebugLogCallback(tflm_debug_log_callback);
    tflite::MicroInterpreter interpreter(
        model, resolver, g_tensor_arena, sizeof(g_tensor_arena));
    if (interpreter.AllocateTensors() != kTfLiteOk)
    {
        tm_putstring((UB *)"AllocateTensors failed.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }
    tm_printf((UB *)"Tensor arena used: %u/%u bytes\n",
              static_cast<unsigned int>(interpreter.arena_used_bytes()),
              static_cast<unsigned int>(kTensorArenaSize));

    TfLiteTensor *input = interpreter.input(0);
    TfLiteTensor *output = interpreter.output(0);
    const int input_shape[] = {1, 256, 256, 3};
    const int output_shape[] = {1, static_cast<int>(kYoloCandidateCount),
                                static_cast<int>(kYoloValuesPerCandidate)};
    if (input == nullptr || output == nullptr ||
        input->type != kTfLiteUInt8 || output->type != kTfLiteUInt8 ||
        input->bytes != kModelInputBytes || output->bytes != kModelOutputBytes ||
        !tensor_shape_matches(input, 4, input_shape) ||
        !tensor_shape_matches(output, 3, output_shape))
    {
        tm_putstring((UB *)"Unexpected model tensor contract.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    if (!fish_bbox_camera_start())
    {
        tm_putstring((UB *)"Camera initialization failed.\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    const bool display_ready = fish_bbox_display_start();
    if (!display_ready)
    {
        tm_putstring((UB *)"Display unavailable; continuing without preview.\n");
    }

    bool batch_detections[kInferenceBatchSize]{};
    float batch_scores[kInferenceBatchSize]{};
    uint32_t batch_frame_sequences[kInferenceBatchSize]{};
    int batch_index = 0;
    int detected_count = 0;
    uint32_t no_frame_wait_count = 0U;
    bool first_inference_reported = false;
    bool first_loop_trace_reported = false;
    uint32_t inference_count = 0U;

    tm_putstring((UB *)"Running fish model on camera at 5 inferences per second.\n");
    while (true)
    {
        if (!first_loop_trace_reported)
        {
            tm_putstring((UB *)"Inference loop entered.\n");
            tm_putstring((UB *)"Calling tk_get_otm.\n");
        }
        SYSTIM cycle_start{};
        if (tk_get_otm(&cycle_start) != E_OK)
        {
            tm_putstring((UB *)"Failed to read system time.\n");
            tk_dly_tsk(kInferencePeriodMs);
            continue;
        }
        if (!first_loop_trace_reported)
        {
            tm_putstring((UB *)"tk_get_otm returned.\n");
            tm_putstring((UB *)"Calling camera input preparation.\n");
        }

        uint32_t frame_sequence = 0U;
        const bool input_ready = fish_bbox_camera_prepare_input(
            input->data.uint8, input->bytes, input->params.scale, input->params.zero_point,
            &frame_sequence);
        if (!first_loop_trace_reported)
        {
            tm_putstring(input_ready ? (UB *)"Camera input preparation returned ready.\n"
                                     : (UB *)"Camera input preparation returned no frame.\n");
            first_loop_trace_reported = true;
        }
        if (!input_ready)
        {
            if (++no_frame_wait_count == 100U)
            {
                tm_putstring((UB *)"Waiting for a completed camera frame.\n");
                fish_bbox_camera_report_status();
                no_frame_wait_count = 0U;
            }
            tk_dly_tsk(10);
            continue;
        }
        no_frame_wait_count = 0U;

        if (!first_inference_reported)
        {
            tm_printf((UB *)"Starting first fish model inference: frame=%u\n",
                      static_cast<unsigned int>(frame_sequence));
        }
        const uint32_t inference_number = inference_count + 1U;
        if (inference_number <= 6U)
        {
            tm_printf((UB *)"Inference cycle %u: Invoke start frame=%u\n",
                      static_cast<unsigned int>(inference_number),
                      static_cast<unsigned int>(frame_sequence));
        }
        const TfLiteStatus invoke_status = interpreter.Invoke();
        if (invoke_status != kTfLiteOk)
        {
            tm_putstring((UB *)"Model Invoke failed.\n");
            tk_slp_tsk(TMO_FEVR);
            return;
        }
        ++inference_count;
        if (inference_number <= 6U)
        {
            tm_printf((UB *)"Inference cycle %u: Invoke done\n",
                      static_cast<unsigned int>(inference_number));
        }
        if (!first_inference_reported)
        {
            tm_putstring((UB *)"First fish model inference completed.\n");
            first_inference_reported = true;
        }

        Detection selected_detection{};
        const bool detected = select_center_detection(output, &selected_detection);
        const FishObservation fish = {
            detected,
            selected_detection.left,
            camera_y_from_model(selected_detection.top),
            selected_detection.width,
            camera_height_from_model(selected_detection.height),
        };
        const FeedingMonitorResult feeding = g_feeding_monitor.process(
            fish_bbox_camera_latest_frame(), kCameraStrideBytes, cycle_start.lo, fish);
        if (inference_number <= 6U)
        {
            tm_printf((UB *)"Inference cycle %u: postprocess done detected=%u\n",
                      static_cast<unsigned int>(inference_number),
                      static_cast<unsigned int>(detected));
        }
        if (display_ready)
        {
            fish_bbox_display_render(fish_bbox_camera_latest_frame(),
                                     selected_detection.left, selected_detection.top,
                                     selected_detection.width, selected_detection.height,
                                     detected, feeding);
        }
        if (inference_number <= 6U)
        {
            tm_printf((UB *)"Inference cycle %u: display done\n",
                      static_cast<unsigned int>(inference_number));
        }
        batch_detections[batch_index] = detected;
        batch_scores[batch_index] = detected ? selected_detection.score : 0.0f;
        if (batch_detections[batch_index])
        {
            ++detected_count;
        }
        batch_frame_sequences[batch_index] = frame_sequence;
        ++batch_index;

        if (batch_index == kInferenceBatchSize)
        {
            const unsigned int food_x = feeding.food.candidate && feeding.food.x >= 0.0f &&
                                                feeding.food.x < 640.0f
                                            ? static_cast<unsigned int>(feeding.food.x)
                                            : 0xffffU;
            const unsigned int food_y = feeding.food.candidate && feeding.food.y >= 0.0f &&
                                                feeding.food.y < 480.0f
                                            ? static_cast<unsigned int>(feeding.food.y)
                                            : 0xffffU;
            tm_printf((UB *)"1s detections=%d/5 frames=%u-%u vin_extra=%u feeding=%s gap=%s age=%us food=%c@%u,%u/%u",
                      detected_count,
                      static_cast<unsigned int>(batch_frame_sequences[0]),
                      static_cast<unsigned int>(batch_frame_sequences[kInferenceBatchSize - 1]),
                      static_cast<unsigned int>(fish_bbox_camera_extra_notify_count()),
                      feeding_state_name(feeding.state), feeding_gap_state_name(feeding.gap_state),
                      static_cast<unsigned int>(feeding.seconds_since_feeding),
                      feeding.food.candidate ? 'Y' : '-', food_x, food_y,
                      static_cast<unsigned int>(feeding.food.area));
            for (int sample = 0; sample < kInferenceBatchSize; ++sample)
            {
                tm_printf((UB *)" #%d%c=%.2f", sample + 1,
                          batch_detections[sample] ? 'D' : '-', batch_scores[sample]);
            }
            tm_putstring((UB *)"\n");
            batch_index = 0;
            detected_count = 0;
        }

        wait_for_next_inference(cycle_start);
        if (!fish_bbox_camera_capture_start())
        {
            tm_putstring((UB *)"Failed to arm VIN for the next inference.\n");
            tk_slp_tsk(TMO_FEVR);
            return;
        }
        if (inference_number <= 6U)
        {
            tm_printf((UB *)"Inference cycle %u: next capture armed\n",
                      static_cast<unsigned int>(inference_number));
        }
    }
}