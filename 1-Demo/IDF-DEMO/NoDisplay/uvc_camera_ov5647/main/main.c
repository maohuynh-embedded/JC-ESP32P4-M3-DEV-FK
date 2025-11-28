/*
 * Main Entry Point - UVC Camera Application
 * Reference Architecture Pattern
 *
 * Architecture: Table-driven FreeRTOS multi-task system
 * Based on: /mnt/e/4_WEEV/01_SRC/01_Embedded_sub_esp32_wifi_node
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "os_interface.h"
#include "uvc_app_common.h"

#ifdef DEBUG_MENLEAK
#include "esp_heap_caps.h"

static void monitor_all_task_stacks(void)
{
    char pcTaskBuffer[1024];
    vTaskList(pcTaskBuffer);
    ESP_LOGI(APP_TAG, "Task List:\n%s", pcTaskBuffer);
}

static void monitor_heap(void)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

    ESP_LOGW(APP_TAG, "Heap Free: %lu (%.2f%%) | Min: %lu (%.2f%%) | Largest Block: %lu",
             free_heap, (float)free_heap / total_heap * 100,
             min_heap, (float)min_heap / total_heap * 100,
             largest_block);
}
#endif

void app_main(void)
{
    ESP_LOGI(APP_TAG, "========================================");
    ESP_LOGI(APP_TAG, "  UVC Camera Application");
    ESP_LOGI(APP_TAG, "  Architecture: Reference Pattern");
    ESP_LOGI(APP_TAG, "  FreeRTOS Multi-Task System");
    ESP_LOGI(APP_TAG, "========================================");

#ifdef DEBUG_MENLEAK
    monitor_heap();
#endif

    /*
     * os_startup() will:
     * 1. Call os_init_stuff() - Global initialization
     * 2. Call each task's initfunc() - Task-specific init
     * 3. Create all queues
     * 4. Create all tasks from taskcfg_tb[]
     */
    os_startup();

    ESP_LOGI(APP_TAG, "========================================");
    ESP_LOGI(APP_TAG, "  System initialized successfully!");
    ESP_LOGI(APP_TAG, "  All tasks are running...");
    ESP_LOGI(APP_TAG, "  Waiting for USB connection...");
    ESP_LOGI(APP_TAG, "========================================");

    /* Main loop - periodic monitoring */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10 seconds

#ifdef DEBUG_MENLEAK
        monitor_all_task_stacks();
        monitor_heap();
#endif
    }
}
