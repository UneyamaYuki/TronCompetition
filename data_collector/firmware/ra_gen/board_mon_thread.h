/* generated thread header file - do not edit */
#ifndef BOARD_MON_THREAD_H_
#define BOARD_MON_THREAD_H_
#include "bsp_api.h"
                #include "FreeRTOS.h"
                #include "task.h"
                #include "semphr.h"
                #include "hal_data.h"
                #ifdef __cplusplus
                extern "C" void board_mon_thread_entry(void * pvParameters);
                #else
                extern void board_mon_thread_entry(void * pvParameters);
                #endif
FSP_HEADER
FSP_FOOTER
#endif /* BOARD_MON_THREAD_H_ */
