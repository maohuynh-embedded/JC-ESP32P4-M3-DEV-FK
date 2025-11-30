/*
 * OS Configuration - Task Table and Initialization
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "os_service.h"
#include "os_interface.h"
#include "uvc_app_common.h"

/* Task entry points - declared in respective task modules */
extern void initUvcStreamTask(void *arg);
extern void mainUvcStreamTask(void *arg);
extern void terUvcStreamTask(void *arg);

extern void initMonitorTask(void *arg);
extern void mainMonitorTask(void *arg);
extern void terMonitorTask(void *arg);

extern void initEventHandlerTask(void *arg);
extern void mainEventHandlerTask(void *arg);
extern void terEventHandlerTask(void *arg);

static const char *TAG = "os_cfg";

/* Task priority definitions */
#define TASK_PRIORITY_UVC_STREAM    5  /* Highest: handles camera+encode+stream in callbacks */
#define TASK_PRIORITY_EVENT         2
#define TASK_PRIORITY_MONITOR       1

/* Task stack sizes */
#define STACK_SIZE_UVC_STREAM       (8 * 1024)  /* Increased: handles camera+encode+stream */
#define STACK_SIZE_EVENT            (4 * 1024)
#define STACK_SIZE_MONITOR          (4 * 1024)

/* Task configuration table */
const taskcfg_st taskcfg_tb[NUMOFTASK] = {
    /* taskname         initfunc            mainfunc            terfunc             stacksize               priority                core */
    {"uvc_stream",      initUvcStreamTask,  mainUvcStreamTask,  terUvcStreamTask,   STACK_SIZE_UVC_STREAM,  TASK_PRIORITY_UVC_STREAM, 0},
    {"monitor",         initMonitorTask,    mainMonitorTask,    terMonitorTask,     STACK_SIZE_MONITOR,     TASK_PRIORITY_MONITOR,  0},
    {"event",           initEventHandlerTask, mainEventHandlerTask, terEventHandlerTask, STACK_SIZE_EVENT,   TASK_PRIORITY_EVENT,    0},
};

/* Global initialization - called before tasks are created */
void os_init_stuff(void)
{
    ESP_LOGI(TAG, "Initializing common subsystems...");

    /* Initialize video hardware */
    uvc_app_hw_init();

    /* Initialize debug if enabled */
#ifdef CONFIG_CAMERA_DEBUG_ENABLE
    uvc_app_debug_init();
#endif

    ESP_LOGI(TAG, "Common subsystems initialized");
}

/* Global termination - called when shutting down */
void os_terminate_stuff(void)
{
    uint16_t idx;

    ESP_LOGI(TAG, "Terminating all tasks...");

    for (idx = 0; idx < NUMOFTASK; idx++) {
        if (taskcfg_tb[idx].terfunc != NULL) {
            taskcfg_tb[idx].terfunc(NULL);
        }
    }

    ESP_LOGI(TAG, "All tasks terminated");
}
