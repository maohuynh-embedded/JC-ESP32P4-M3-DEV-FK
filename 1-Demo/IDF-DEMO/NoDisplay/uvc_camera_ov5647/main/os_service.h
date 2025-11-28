/*
 * OS Service - Task Management Infrastructure
 * Based on reference architecture pattern
 */

#ifndef OS_SERVICE_H
#define OS_SERVICE_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task configuration structure */
typedef struct {
    char*           taskname;
    TaskFunction_t  initfunc;       // Init phase function
    TaskFunction_t  mainfunc;       // Main task loop function
    TaskFunction_t  terfunc;        // Terminate/cleanup function
    uint32_t        stacksize;
    uint16_t        priority;
    int             core;           // CPU core affinity (0 or 1)
} taskcfg_st;

/* Task handler storage */
typedef struct {
    TaskHandle_t    handler;
} taskhdler_st;

/* Queue configuration */
#define OS_QUEUE_MAX_ITEMS      10
#define OS_QUEUE_ITEM_SIZE      sizeof(void*)  // Queue holds pointers

#ifdef __cplusplus
}
#endif

#endif /* OS_SERVICE_H */
