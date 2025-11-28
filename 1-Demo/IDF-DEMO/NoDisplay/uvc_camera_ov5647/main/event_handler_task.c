/*
 * Event Handler Task
 *
 * Responsibilities:
 * - Handle system events
 * - Manage application state machine
 * - Process commands from UVC callbacks
 * - Coordinate between tasks
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "uvc_app_common.h"
#include "os_interface.h"

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
#include "camera_debug.h"
#endif

/* Task context */
typedef struct {
    uint32_t events_processed;
    bool initialized;
} event_handler_task_ctx_t;

static event_handler_task_ctx_t s_evt_ctx = {0};

/* ========== Init Phase ========== */
void initEventHandlerTask(void *arg)
{
    ESP_LOGI(EVT_TAG, "Initializing event handler task...");

    s_evt_ctx.events_processed = 0;
    s_evt_ctx.initialized = false;

    s_evt_ctx.initialized = true;
    ESP_LOGI(EVT_TAG, "Event handler task initialized");
}

/* ========== Main Loop ========== */
void mainEventHandlerTask(void *arg)
{
    system_event_t event;
    QueueHandle_t event_queue;

    ESP_LOGI(EVT_TAG, "Event handler task started on core %d", xPortGetCoreID());

    /* Get event queue */
    event_queue = os_getQueueHandler(QUEUE_SYSTEM_EVENT);
    if (!event_queue) {
        ESP_LOGE(EVT_TAG, "Failed to get event queue");
        goto exit;
    }

    /* Main loop */
    while (1) {
        /* Check for shutdown */
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(EVT_TAG, "Shutdown requested");
            break;
        }

        /* Wait for events */
        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        ESP_LOGI(EVT_TAG, "Processing event type: %d", event.type);

        /* Process event */
        switch (event.type) {
            case SYS_EVENT_START_STREAM:
                ESP_LOGI(EVT_TAG, "Starting stream...");
                xEventGroupSetBits(g_app_ctx.system_events, EVENT_STREAMING_ACTIVE);
                g_app_ctx.is_streaming = true;
                ESP_LOGI(EVT_TAG, "Streaming ACTIVE");
                break;

            case SYS_EVENT_STOP_STREAM:
                ESP_LOGI(EVT_TAG, "Stopping stream...");
                xEventGroupClearBits(g_app_ctx.system_events, EVENT_STREAMING_ACTIVE);
                g_app_ctx.is_streaming = false;
                ESP_LOGI(EVT_TAG, "Streaming STOPPED");
                break;

            case SYS_EVENT_RESET_STATS:
                ESP_LOGI(EVT_TAG, "Resetting statistics...");
                g_app_ctx.total_frames_captured = 0;
                g_app_ctx.total_frames_encoded = 0;
                g_app_ctx.total_frames_streamed = 0;
                g_app_ctx.frames_dropped = 0;

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
                camera_debug_reset_stats();
#endif
                ESP_LOGI(EVT_TAG, "Statistics reset complete");
                break;

            case SYS_EVENT_CHANGE_FORMAT:
                ESP_LOGI(EVT_TAG, "Format change requested");
                ESP_LOGW(EVT_TAG, "Format change not implemented yet");
                // TODO: Implement format change logic
                // 1. Stop streaming
                // 2. Reconfigure encoder
                // 3. Restart streaming
                break;

            case SYS_EVENT_CHANGE_RESOLUTION:
                ESP_LOGI(EVT_TAG, "Resolution change requested");
                ESP_LOGW(EVT_TAG, "Resolution change not implemented yet");
                // TODO: Implement resolution change logic
                // 1. Stop streaming
                // 2. Reconfigure camera and encoder
                // 3. Restart streaming
                break;

            case SYS_EVENT_ERROR:
                ESP_LOGE(EVT_TAG, "System error event received");
                if (event.data) {
                    ESP_LOGE(EVT_TAG, "Error data: %s", (char*)event.data);
                }
                // TODO: Implement error recovery logic
                break;

            default:
                ESP_LOGW(EVT_TAG, "Unknown event type: %d", event.type);
                break;
        }

        /* Free event data if allocated */
        if (event.data && event.data_len > 0) {
            free(event.data);
        }

        s_evt_ctx.events_processed++;
    }

exit:
    ESP_LOGI(EVT_TAG, "Event handler task exiting");
    vTaskDelete(NULL);
}

/* ========== Terminate Phase ========== */
void terEventHandlerTask(void *arg)
{
    ESP_LOGI(EVT_TAG, "Terminating event handler task...");
    ESP_LOGI(EVT_TAG, "Processed %lu events", s_evt_ctx.events_processed);

    /* Cleanup */
    s_evt_ctx.initialized = false;
}
