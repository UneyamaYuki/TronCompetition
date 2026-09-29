#include "fish_bbox_npu_test.hpp"

#include "ai/model/fish_bbox_model_data.hpp"
#include "ai/fish_bbox_camera.hpp"
#include "ai/fish_bbox_display.hpp"
#include "ai/feeding_monitor.hpp"
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
constexpr size_t kModelInputBytes = 256U * 256U;
constexpr int kOutputCount = 5;
constexpr int kInferenceBatchSize = 5;
constexpr RELTIM kInferencePeriodMs = 200;
constexpr float kDetectionConfidenceThreshold = 0.5f;
constexpr size_t kEthosuSemaphoreCount = 3U;
constexpr size_t kCameraStrideBytes = 2048U;

struct TKernelSemaphore
{
    ID id;
    bool allocated;
};

alignas(64) uint8_t g_tensor_arena[kTensorArenaSize]
    __attribute__((section(".sdram_nocache")));
TKernelSemaphore g_ethosu_semaphores[kEthosuSemaphoreCount]{};
FeedingMonitor g_feeding_monitor{};

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
}

extern "C" void *ethosu_mutex_create(void)
{
    return create_ethosu_semaphore(1);
}

extern "C" void ethosu_mutex_destroy(void *mutex)
{
    destroy_ethosu_semaphore(mutex);
}

extern "C" int ethosu_mutex_lock(void *mutex)
{
    TKernelSemaphore *semaphore = get_ethosu_semaphore(mutex);
    return semaphore != nullptr && tk_wai_sem(semaphore->id, 1, TMO_FEVR) == E_OK ? 0 : -1;
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
    const TMO wait_time = timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER ?
                          TMO_FEVR : static_cast<TMO>(timeout);
    return semaphore != nullptr && tk_wai_sem(semaphore->id, 1, wait_time) == E_OK ? 0 : -1;
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

    const tflite::Model *model = tflite::GetModel(g_fish_bbox_model_data);
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
    const int input_shape[] = {1, 256, 256, 1};
    const int output_shape[] = {1, kOutputCount};
    if (input == nullptr || output == nullptr ||
        input->type != kTfLiteInt8 || output->type != kTfLiteInt8 ||
        input->bytes != kModelInputBytes || output->bytes != kOutputCount ||
        !tensor_shape_matches(input, 4, input_shape) ||
        !tensor_shape_matches(output, 2, output_shape))
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

    int8_t batch_outputs[kInferenceBatchSize][kOutputCount]{};
    bool batch_detections[kInferenceBatchSize]{};
    uint32_t batch_frame_sequences[kInferenceBatchSize]{};
    int batch_index = 0;
    int detected_count = 0;
    uint32_t no_frame_wait_count = 0U;
    bool first_inference_reported = false;
    bool first_loop_trace_reported = false;

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
            input->data.int8, input->bytes, input->params.scale, input->params.zero_point,
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
        if (interpreter.Invoke() != kTfLiteOk)
        {
            tm_putstring((UB *)"Model Invoke failed.\n");
            tk_slp_tsk(TMO_FEVR);
            return;
        }
        if (!first_inference_reported)
        {
            tm_putstring((UB *)"First fish model inference completed.\n");
            first_inference_reported = true;
        }

        int32_t confidence_quantized = output->data.int8[4];
        const float confidence =
            static_cast<float>(confidence_quantized - output->params.zero_point) *
            output->params.scale;
        const bool detected = confidence >= kDetectionConfidenceThreshold;
        auto dequantize_unit = [output](int index) {
            const float value =
                static_cast<float>(output->data.int8[index] - output->params.zero_point) *
                output->params.scale;
            return std::max(0.0f, std::min(1.0f, value));
        };
        const FishObservation fish = {
            detected,
            dequantize_unit(0), dequantize_unit(1),
            dequantize_unit(2), dequantize_unit(3),
        };
        const FeedingMonitorResult feeding = g_feeding_monitor.process(
            fish_bbox_camera_latest_frame(), kCameraStrideBytes, cycle_start.lo, fish);
        if (display_ready)
        {
            fish_bbox_display_render(fish_bbox_camera_latest_frame(),
                                     dequantize_unit(0), dequantize_unit(1),
                                     dequantize_unit(2), dequantize_unit(3), detected, feeding);
        }
        batch_detections[batch_index] = detected;
        if (batch_detections[batch_index])
        {
            ++detected_count;
        }
        for (int index = 0; index < kOutputCount; ++index)
        {
            batch_outputs[batch_index][index] = output->data.int8[index];
        }
        batch_frame_sequences[batch_index] = frame_sequence;
        ++batch_index;

        if (batch_index == kInferenceBatchSize)
        {
            tm_printf((UB *)"1s detections=%d/5 frames=%u-%u feeding=%s gap=%s age=%us",
                      detected_count,
                      static_cast<unsigned int>(batch_frame_sequences[0]),
                      static_cast<unsigned int>(batch_frame_sequences[kInferenceBatchSize - 1]),
                      feeding_state_name(feeding.state), feeding_gap_state_name(feeding.gap_state),
                      static_cast<unsigned int>(feeding.seconds_since_feeding));
            for (int sample = 0; sample < kInferenceBatchSize; ++sample)
            {
                tm_printf((UB *)" #%d%c=[", sample + 1,
                          batch_detections[sample] ? 'D' : '-');
                for (int index = 0; index < kOutputCount; ++index)
                {
                    tm_printf((UB *)"%s%d", index == 0 ? "" : ",",
                              (int)batch_outputs[sample][index]);
                }
                tm_putstring((UB *)"]");
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
    }
}