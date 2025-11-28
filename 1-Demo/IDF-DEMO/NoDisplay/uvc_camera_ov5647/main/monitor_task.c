/*
 * Monitor Task
 *
 * Responsibilities:
 * - Monitor system performance
 * - Track frame statistics
 * - Print periodic reports
 * - Monitor memory usage
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "uvc_app_common.h"
#include "os_interface.h"

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
#include "camera_debug.h"
#endif

/* Task context */
typedef struct {
    uint32_t report_count;
    TickType_t last_report_time;
} monitor_task_ctx_t;

static monitor_task_ctx_t s_mon_ctx = {0};

/* Monitor interval in milliseconds */
#define MONITOR_INTERVAL_MS     5000

/* ========== Init Phase ========== */
void initMonitorTask(void *arg)
{
    ESP_LOGI(MON_TAG, "Initializing monitor task...");

    s_mon_ctx.report_count = 0;
    s_mon_ctx.last_report_time = xTaskGetTickCount();

    ESP_LOGI(MON_TAG, "Monitor task initialized");
}

/* ========== Main Loop ========== */
void mainMonitorTask(void *arg)
{
    TickType_t last_wake_time;
    const TickType_t interval = pdMS_TO_TICKS(MONITOR_INTERVAL_MS);

    ESP_LOGI(MON_TAG, "Monitor task started on core %d", xPortGetCoreID());

    last_wake_time = xTaskGetTickCount();

    /* Main loop */
    while (1) {
        /* Check for shutdown */
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(MON_TAG, "Shutdown requested");
            break;
        }

        /* Wait for next monitoring interval */
        vTaskDelayUntil(&last_wake_time, interval);

        /* Print system monitor report */
        ESP_LOGI(MON_TAG, "========== System Monitor ==========");
        ESP_LOGI(MON_TAG, "Streaming:  %s", g_app_ctx.is_streaming ? "ACTIVE" : "IDLE");
        ESP_LOGI(MON_TAG, "Captured:   %lu frames", g_app_ctx.total_frames_captured);
        ESP_LOGI(MON_TAG, "Encoded:    %lu frames", g_app_ctx.total_frames_encoded);
        ESP_LOGI(MON_TAG, "Streamed:   %lu frames", g_app_ctx.total_frames_streamed);
        ESP_LOGI(MON_TAG, "Dropped:    %lu frames", g_app_ctx.frames_dropped);

        /* Queue status */
        QueueHandle_t raw_queue = os_getQueueHandler(QUEUE_RAW_FRAME);
        QueueHandle_t enc_queue = os_getQueueHandler(QUEUE_ENCODED_FRAME);

        if (raw_queue) {
            UBaseType_t raw_waiting = uxQueueMessagesWaiting(raw_queue);
            ESP_LOGI(MON_TAG, "Raw queue:  %u/10 messages", raw_waiting);
        }

        if (enc_queue) {
            UBaseType_t enc_waiting = uxQueueMessagesWaiting(enc_queue);
            ESP_LOGI(MON_TAG, "Enc queue:  %u/10 messages", enc_waiting);
        }

        /* Memory info */
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t min_free = esp_get_minimum_free_heap_size();
        uint32_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        ESP_LOGI(MON_TAG, "Free heap:  %lu bytes (%.2f MB)", free_heap, free_heap / 1048576.0);
        ESP_LOGI(MON_TAG, "Min free:   %lu bytes (%.2f MB)", min_free, min_free / 1048576.0);
        ESP_LOGI(MON_TAG, "Free PSRAM: %lu bytes (%.2f MB)", free_spiram, free_spiram / 1048576.0);

        /* Task stack high water marks */
        TaskHandle_t cam_task = os_getTaskHandler(TASK_CAMERA_CAPTURE);
        TaskHandle_t enc_task = os_getTaskHandler(TASK_ENCODING);
        TaskHandle_t uvc_task = os_getTaskHandler(TASK_UVC_STREAM);
        TaskHandle_t evt_task = os_getTaskHandler(TASK_EVENT_HANDLER);

        if (cam_task) {
            UBaseType_t cam_hwm = uxTaskGetStackHighWaterMark(cam_task);
            ESP_LOGI(MON_TAG, "Camera stack:   %u bytes free", cam_hwm * sizeof(StackType_t));
        }

        if (enc_task) {
            UBaseType_t enc_hwm = uxTaskGetStackHighWaterMark(enc_task);
            ESP_LOGI(MON_TAG, "Encoding stack: %u bytes free", enc_hwm * sizeof(StackType_t));
        }

        if (uvc_task) {
            UBaseType_t uvc_hwm = uxTaskGetStackHighWaterMark(uvc_task);
            ESP_LOGI(MON_TAG, "UVC stack:      %u bytes free", uvc_hwm * sizeof(StackType_t));
        }

        if (evt_task) {
            UBaseType_t evt_hwm = uxTaskGetStackHighWaterMark(evt_task);
            ESP_LOGI(MON_TAG, "Event stack:    %u bytes free", evt_hwm * sizeof(StackType_t));
        }

        UBaseType_t mon_hwm = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI(MON_TAG, "Monitor stack:  %u bytes free", mon_hwm * sizeof(StackType_t));

        ESP_LOGI(MON_TAG, "====================================");

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
        /* Print camera debug statistics if enabled */
        camera_debug_print_stats();
#endif

        s_mon_ctx.report_count++;
    }

    ESP_LOGI(MON_TAG, "Monitor task exiting");
    vTaskDelete(NULL);
}

/* ========== Terminate Phase ========== */
void terMonitorTask(void *arg)
{
    ESP_LOGI(MON_TAG, "Terminating monitor task...");
    ESP_LOGI(MON_TAG, "Printed %lu monitor reports", s_mon_ctx.report_count);
}
