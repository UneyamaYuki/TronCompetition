/* generated thread header file - do not edit */
#ifndef CAMERA_THREAD_H_
#define CAMERA_THREAD_H_
#include "bsp_api.h"
                #include "FreeRTOS.h"
                #include "task.h"
                #include "semphr.h"
                #include "hal_data.h"
                #ifdef __cplusplus
                extern "C" void camera_thread_entry(void * pvParameters);
                #else
                extern void camera_thread_entry(void * pvParameters);
                #endif
FSP_HEADER
FSP_FOOTER
#endif /* CAMERA_THREAD_H_ */
