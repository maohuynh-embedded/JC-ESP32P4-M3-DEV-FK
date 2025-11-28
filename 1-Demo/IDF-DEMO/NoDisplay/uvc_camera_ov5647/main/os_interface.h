/*
 * OS Interface - Public API for task and queue management
 */

#ifndef OS_INTERFACE_H
#define OS_INTERFACE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task IDs */
typedef enum {
    TASK_CAMERA_CAPTURE,
    TASK_ENCODING,
    TASK_UVC_STREAM,
    TASK_MONITOR,
    TASK_EVENT_HANDLER,
    /* Add new tasks above this line */
    NUMOFTASK
} os_task_id_en;

/* Queue IDs */
typedef enum {
    QUEUE_RAW_FRAME,
    QUEUE_ENCODED_FRAME,
    QUEUE_SYSTEM_EVENT,
    /* Add new queues above this line */
    NUMOFQUEUE
} os_queue_id_en;

/* Public API */
void os_startup(void);
void os_terminate_stuff(void);
TaskHandle_t os_getTaskHandler(os_task_id_en task_id);
QueueHandle_t os_getQueueHandler(os_queue_id_en queue_id);

#ifdef __cplusplus
}
#endif

#endif /* OS_INTERFACE_H */
