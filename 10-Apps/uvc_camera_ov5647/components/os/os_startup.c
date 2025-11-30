/*
 * OS Startup - Task and Queue Creation
 * Based on reference architecture pattern
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "os_service.h"
#include "os_interface.h"

static const char *TAG = "os_startup";

extern const taskcfg_st taskcfg_tb[];
extern void os_init_stuff(void);

/* Storage for task handles and queue handles */
taskhdler_st taskbox[NUMOFTASK] = {0};
QueueHandle_t queuebox[NUMOFQUEUE] = {0};

void os_startup(void)
{
    uint16_t idx;
    BaseType_t ret;
    TaskHandle_t taskhdl;
    QueueHandle_t queuehdl;

    // Run initialization functions
    ESP_LOGI(TAG, "Application starting up...");
    os_init_stuff();

    // Call individual task init functions
    for (idx = 0; idx < NUMOFTASK; idx++) {
        if (taskcfg_tb[idx].initfunc != NULL) {
            ESP_LOGI(TAG, "Calling init for task: %s", taskcfg_tb[idx].taskname);
            taskcfg_tb[idx].initfunc(NULL);
        }
    }

    // Create queues
    ESP_LOGI(TAG, "Creating queues...");
    for (idx = 0; idx < NUMOFQUEUE; idx++) {
        queuehdl = xQueueCreate(OS_QUEUE_MAX_ITEMS, OS_QUEUE_ITEM_SIZE);

        if (queuehdl != NULL) {
            queuebox[idx] = queuehdl;
            ESP_LOGI(TAG, "Queue[%d] created successfully", idx);
        } else {
            ESP_LOGE(TAG, "Failed to create Queue index %d", idx);
        }
    }

    // Create tasks
    ESP_LOGI(TAG, "Creating tasks...");
    for (idx = 0; idx < NUMOFTASK; idx++) {
        if (taskcfg_tb[idx].mainfunc != NULL) {
            ret = xTaskCreatePinnedToCore(
                taskcfg_tb[idx].mainfunc,
                taskcfg_tb[idx].taskname,
                taskcfg_tb[idx].stacksize,
                NULL,
                taskcfg_tb[idx].priority,
                &taskhdl,
                taskcfg_tb[idx].core
            );

            if (ret == pdPASS) {
                taskbox[idx].handler = taskhdl;
                ESP_LOGI(TAG, "Task '%s' created successfully on core %d",
                         taskcfg_tb[idx].taskname, taskcfg_tb[idx].core);
            } else {
                ESP_LOGE(TAG, "Failed to create task: %s", taskcfg_tb[idx].taskname);
            }
        } else {
            /* No thread requested */
            taskbox[idx].handler = NULL;
        }
    }

    ESP_LOGI(TAG, "OS startup complete");
}

TaskHandle_t os_getTaskHandler(os_task_id_en task_id)
{
    TaskHandle_t ret = NULL;
    if (task_id < NUMOFTASK) {
        ret = taskbox[task_id].handler;
    }
    return ret;
}

QueueHandle_t os_getQueueHandler(os_queue_id_en queue_id)
{
    QueueHandle_t ret = NULL;
    if (queue_id < NUMOFQUEUE) {
        ret = queuebox[queue_id];
    }
    return ret;
}
