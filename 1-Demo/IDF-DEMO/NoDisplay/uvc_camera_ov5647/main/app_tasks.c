/*
 * FreeRTOS Multi-Task Application Implementation
 */

#include "app_tasks.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <sys/ioctl.h>
#include "linux/videodev2.h"

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
#include "camera_debug.h"
#endif

static const char *TAG = "app_tasks";

/* ========== Frame Buffer Management ========== */

frame_buffer_t *frame_buffer_alloc(size_t capacity)
{
    frame_buffer_t *frame = heap_caps_malloc(sizeof(frame_buffer_t), MALLOC_CAP_DEFAULT);
    if (!frame) {
        ESP_LOGE(TAG, "Failed to allocate frame structure");
        return NULL;
    }

    frame->data = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
    if (!frame->data) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        free(frame);
        return NULL;
    }

    frame->size = 0;
    frame->capacity = capacity;
    frame->timestamp = 0;
    frame->frame_number = 0;
    frame->format = 0;

    return frame;
}

void frame_buffer_free(frame_buffer_t *frame)
{
    if (frame) {
        if (frame->data) {
            free(frame->data);
        }
        free(frame);
    }
}

esp_err_t frame_buffer_resize(frame_buffer_t *frame, size_t new_capacity)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    if (new_capacity == frame->capacity) {
        return ESP_OK;
    }

    uint8_t *new_data = heap_caps_realloc(frame->data, new_capacity, MALLOC_CAP_SPIRAM);
    if (!new_data) {
        ESP_LOGE(TAG, "Failed to resize frame buffer");
        return ESP_ERR_NO_MEM;
    }

    frame->data = new_data;
    frame->capacity = new_capacity;

    return ESP_OK;
}

/* ========== Event Posting ========== */

esp_err_t app_post_event(app_context_t *ctx, system_event_type_t type, void *data, size_t data_len)
{
    if (!ctx || !ctx->event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    system_event_t event = {
        .type = type,
        .data = data,
        .data_len = data_len
    };

    if (xQueueSend(ctx->event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to post event type %d", type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* ========== Task 1: Camera Capture ========== */

void camera_capture_task(void *pvParameters)
{
    app_context_t *ctx = (app_context_t *)pvParameters;
    ESP_LOGI(TAG, "Camera capture task started");

    // Wait for camera to be ready
    xEventGroupWaitBits(ctx->system_events, EVENT_CAMERA_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    struct v4l2_buffer cap_buf;
    uint32_t frame_number = 0;

    while (1) {
        // Check for shutdown
        EventBits_t bits = xEventGroupGetBits(ctx->system_events);
        if (bits & EVENT_SHUTDOWN) {
            ESP_LOGI(TAG, "Camera task shutting down");
            break;
        }

        // Wait for streaming to be active
        if (!(bits & EVENT_STREAMING_ACTIVE)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Capture frame from camera
        xSemaphoreTake(ctx->camera_mutex, portMAX_DELAY);

        memset(&cap_buf, 0, sizeof(cap_buf));
        cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cap_buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(ctx->uvc->cap_fd, VIDIOC_DQBUF, &cap_buf) != 0) {
            xSemaphoreGive(ctx->camera_mutex);
            ESP_LOGW(TAG, "Failed to dequeue camera buffer");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Allocate frame buffer
        frame_buffer_t *frame = frame_buffer_alloc(cap_buf.bytesused);
        if (!frame) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer");
            ioctl(ctx->uvc->cap_fd, VIDIOC_QBUF, &cap_buf);
            xSemaphoreGive(ctx->camera_mutex);
            ctx->frames_dropped++;
            continue;
        }

        // Copy frame data
        memcpy(frame->data, ctx->uvc->cap_buffer[cap_buf.index], cap_buf.bytesused);
        frame->size = cap_buf.bytesused;
        frame->timestamp = esp_timer_get_time();
        frame->frame_number = frame_number++;

        // Return buffer to camera
        ioctl(ctx->uvc->cap_fd, VIDIOC_QBUF, &cap_buf);
        xSemaphoreGive(ctx->camera_mutex);

        ctx->total_frames_captured++;

        // Send to encoding queue
        if (xQueueSend(ctx->raw_frame_queue, &frame, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Raw frame queue full, dropping frame #%lu", frame->frame_number);
            frame_buffer_free(frame);
            ctx->frames_dropped++;
        } else {
            ESP_LOGD(TAG, "Captured frame #%lu (%zu bytes)", frame->frame_number, frame->size);
        }

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(NULL);
}

/* ========== Task 2: Encoding ========== */

void encoding_task(void *pvParameters)
{
    app_context_t *ctx = (app_context_t *)pvParameters;
    ESP_LOGI(TAG, "Encoding task started");

    // Wait for encoder to be ready
    xEventGroupWaitBits(ctx->system_events, EVENT_ENCODER_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    struct v4l2_buffer m2m_out_buf;
    struct v4l2_buffer m2m_cap_buf;
    struct v4l2_format format;

    while (1) {
        // Check for shutdown
        if (xEventGroupGetBits(ctx->system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(TAG, "Encoding task shutting down");
            break;
        }

        // Wait for raw frame
        frame_buffer_t *raw_frame = NULL;
        if (xQueueReceive(ctx->raw_frame_queue, &raw_frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        xSemaphoreTake(ctx->encoder_mutex, portMAX_DELAY);

        // Send raw frame to encoder input
        memset(&m2m_out_buf, 0, sizeof(m2m_out_buf));
        m2m_out_buf.index  = 0;
        m2m_out_buf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        m2m_out_buf.memory = V4L2_MEMORY_USERPTR;
        m2m_out_buf.m.userptr = (unsigned long)raw_frame->data;
        m2m_out_buf.length = raw_frame->size;

        if (ioctl(ctx->uvc->m2m_fd, VIDIOC_QBUF, &m2m_out_buf) != 0) {
            ESP_LOGW(TAG, "Failed to queue encoder output buffer");
            frame_buffer_free(raw_frame);
            xSemaphoreGive(ctx->encoder_mutex);
            continue;
        }

        // Get encoded frame from encoder
        memset(&m2m_cap_buf, 0, sizeof(m2m_cap_buf));
        m2m_cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        m2m_cap_buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(ctx->uvc->m2m_fd, VIDIOC_DQBUF, &m2m_cap_buf) != 0) {
            ESP_LOGW(TAG, "Failed to dequeue encoder capture buffer");
            ioctl(ctx->uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf);
            frame_buffer_free(raw_frame);
            xSemaphoreGive(ctx->encoder_mutex);
            continue;
        }

        // Allocate encoded frame buffer
        frame_buffer_t *encoded_frame = frame_buffer_alloc(m2m_cap_buf.bytesused);
        if (!encoded_frame) {
            ESP_LOGE(TAG, "Failed to allocate encoded frame buffer");
            ioctl(ctx->uvc->m2m_fd, VIDIOC_QBUF, &m2m_cap_buf);
            ioctl(ctx->uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf);
            frame_buffer_free(raw_frame);
            xSemaphoreGive(ctx->encoder_mutex);
            ctx->frames_dropped++;
            continue;
        }

        // Copy encoded data
        memcpy(encoded_frame->data, ctx->uvc->m2m_cap_buffer, m2m_cap_buf.bytesused);
        encoded_frame->size = m2m_cap_buf.bytesused;
        encoded_frame->timestamp = raw_frame->timestamp;
        encoded_frame->frame_number = raw_frame->frame_number;

        // Get format info
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx->uvc->m2m_fd, VIDIOC_G_FMT, &format);
        encoded_frame->format = format.fmt.pix.pixelformat;

        // Return buffers
        ioctl(ctx->uvc->m2m_fd, VIDIOC_QBUF, &m2m_cap_buf);
        ioctl(ctx->uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf);

        xSemaphoreGive(ctx->encoder_mutex);

        // Free raw frame
        frame_buffer_free(raw_frame);

        ctx->total_frames_encoded++;

        ESP_LOGD(TAG, "Encoded frame #%lu (%zu bytes)", encoded_frame->frame_number, encoded_frame->size);

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
        // Debug encoded frame
        camera_debug_process_frame(encoded_frame->data, encoded_frame->size, encoded_frame->timestamp);
#endif

        // Send to UVC stream queue
        if (xQueueSend(ctx->encoded_frame_queue, &encoded_frame, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Encoded frame queue full, dropping frame #%lu", encoded_frame->frame_number);
            frame_buffer_free(encoded_frame);
            ctx->frames_dropped++;
        }
    }

    vTaskDelete(NULL);
}

/* ========== Task 3: UVC Streaming ========== */

void uvc_stream_task(void *pvParameters)
{
    app_context_t *ctx = (app_context_t *)pvParameters;
    ESP_LOGI(TAG, "UVC stream task started");

    // Wait for UVC to be ready
    xEventGroupWaitBits(ctx->system_events, EVENT_UVC_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    while (1) {
        // Check for shutdown
        if (xEventGroupGetBits(ctx->system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(TAG, "UVC stream task shutting down");
            break;
        }

        // Wait for encoded frame
        frame_buffer_t *frame = NULL;
        if (xQueueReceive(ctx->encoded_frame_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        // TODO: Send frame via UVC
        // This will be integrated with the actual UVC callbacks
        // For now, we just simulate streaming

        ESP_LOGD(TAG, "Streaming frame #%lu (%zu bytes)", frame->frame_number, frame->size);

        ctx->total_frames_streamed++;

        // Free frame
        frame_buffer_free(frame);
    }

    vTaskDelete(NULL);
}

/* ========== Task 4: Monitor ========== */

void monitor_task(void *pvParameters)
{
    app_context_t *ctx = (app_context_t *)pvParameters;
    ESP_LOGI(TAG, "Monitor task started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(5000);  // 5 seconds

    while (1) {
        // Check for shutdown
        if (xEventGroupGetBits(ctx->system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(TAG, "Monitor task shutting down");
            break;
        }

        vTaskDelayUntil(&last_wake_time, interval);

        // Print system statistics
        ESP_LOGI(TAG, "========== System Monitor ==========");
        ESP_LOGI(TAG, "Captured:  %lu frames", ctx->total_frames_captured);
        ESP_LOGI(TAG, "Encoded:   %lu frames", ctx->total_frames_encoded);
        ESP_LOGI(TAG, "Streamed:  %lu frames", ctx->total_frames_streamed);
        ESP_LOGI(TAG, "Dropped:   %lu frames", ctx->frames_dropped);

        // Queue status
        UBaseType_t raw_queue_waiting = uxQueueMessagesWaiting(ctx->raw_frame_queue);
        UBaseType_t enc_queue_waiting = uxQueueMessagesWaiting(ctx->encoded_frame_queue);
        ESP_LOGI(TAG, "Raw queue: %u/%u", raw_queue_waiting, FRAME_QUEUE_SIZE);
        ESP_LOGI(TAG, "Enc queue: %u/%u", enc_queue_waiting, ENCODED_QUEUE_SIZE);

        // Memory info
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free:  %lu bytes", esp_get_minimum_free_heap_size());
        ESP_LOGI(TAG, "====================================");

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
        camera_debug_print_stats();
#endif
    }

    vTaskDelete(NULL);
}

/* ========== Task 5: Event Handler ========== */

void event_handler_task(void *pvParameters)
{
    app_context_t *ctx = (app_context_t *)pvParameters;
    ESP_LOGI(TAG, "Event handler task started");

    system_event_t event;

    while (1) {
        if (xQueueReceive(ctx->event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            // Check for shutdown even when no events
            if (xEventGroupGetBits(ctx->system_events) & EVENT_SHUTDOWN) {
                ESP_LOGI(TAG, "Event handler task shutting down");
                break;
            }
            continue;
        }

        ESP_LOGI(TAG, "Received event type: %d", event.type);

        switch (event.type) {
            case SYS_EVENT_START_STREAM:
                ESP_LOGI(TAG, "Starting stream...");
                xEventGroupSetBits(ctx->system_events, EVENT_STREAMING_ACTIVE);
                ctx->is_streaming = true;
                break;

            case SYS_EVENT_STOP_STREAM:
                ESP_LOGI(TAG, "Stopping stream...");
                xEventGroupClearBits(ctx->system_events, EVENT_STREAMING_ACTIVE);
                ctx->is_streaming = false;
                break;

            case SYS_EVENT_RESET_STATS:
                ESP_LOGI(TAG, "Resetting statistics...");
                ctx->total_frames_captured = 0;
                ctx->total_frames_encoded = 0;
                ctx->total_frames_streamed = 0;
                ctx->frames_dropped = 0;
#ifdef CONFIG_CAMERA_DEBUG_ENABLE
                camera_debug_reset_stats();
#endif
                break;

            case SYS_EVENT_CHANGE_FORMAT:
                ESP_LOGI(TAG, "Format change requested (not implemented)");
                break;

            case SYS_EVENT_CHANGE_RESOLUTION:
                ESP_LOGI(TAG, "Resolution change requested (not implemented)");
                break;

            case SYS_EVENT_ERROR:
                ESP_LOGE(TAG, "System error event received");
                break;

            default:
                ESP_LOGW(TAG, "Unknown event type: %d", event.type);
                break;
        }

        // Free event data if allocated
        if (event.data && event.data_len > 0) {
            free(event.data);
        }
    }

    vTaskDelete(NULL);
}

/* ========== Task Initialization ========== */

esp_err_t app_tasks_init(app_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing application tasks...");

    // Create queues
    ctx->raw_frame_queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(frame_buffer_t *));
    if (!ctx->raw_frame_queue) {
        ESP_LOGE(TAG, "Failed to create raw frame queue");
        return ESP_FAIL;
    }

    ctx->encoded_frame_queue = xQueueCreate(ENCODED_QUEUE_SIZE, sizeof(frame_buffer_t *));
    if (!ctx->encoded_frame_queue) {
        ESP_LOGE(TAG, "Failed to create encoded frame queue");
        return ESP_FAIL;
    }

    ctx->event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(system_event_t));
    if (!ctx->event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }

    // Create semaphores
    ctx->camera_mutex = xSemaphoreCreateMutex();
    if (!ctx->camera_mutex) {
        ESP_LOGE(TAG, "Failed to create camera mutex");
        return ESP_FAIL;
    }

    ctx->encoder_mutex = xSemaphoreCreateMutex();
    if (!ctx->encoder_mutex) {
        ESP_LOGE(TAG, "Failed to create encoder mutex");
        return ESP_FAIL;
    }

    // Create event group
    ctx->system_events = xEventGroupCreate();
    if (!ctx->system_events) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Application tasks initialized successfully");

    return ESP_OK;
}

esp_err_t app_tasks_start(app_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting application tasks...");

    // Create camera capture task
    BaseType_t ret = xTaskCreatePinnedToCore(
        camera_capture_task,
        "camera_task",
        CAMERA_TASK_STACK_SIZE,
        ctx,
        CAMERA_TASK_PRIORITY,
        &ctx->camera_task_handle,
        1  // Pin to core 1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera task");
        return ESP_FAIL;
    }

    // Create encoding task
    ret = xTaskCreatePinnedToCore(
        encoding_task,
        "encoding_task",
        ENCODING_TASK_STACK_SIZE,
        ctx,
        ENCODING_TASK_PRIORITY,
        &ctx->encoding_task_handle,
        1  // Pin to core 1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create encoding task");
        return ESP_FAIL;
    }

    // Create UVC stream task
    ret = xTaskCreatePinnedToCore(
        uvc_stream_task,
        "uvc_stream_task",
        UVC_STREAM_TASK_STACK_SIZE,
        ctx,
        UVC_STREAM_TASK_PRIORITY,
        &ctx->uvc_stream_task_handle,
        0  // Pin to core 0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UVC stream task");
        return ESP_FAIL;
    }

    // Create monitor task
    ret = xTaskCreatePinnedToCore(
        monitor_task,
        "monitor_task",
        MONITOR_TASK_STACK_SIZE,
        ctx,
        MONITOR_TASK_PRIORITY,
        &ctx->monitor_task_handle,
        0  // Pin to core 0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_FAIL;
    }

    // Create event handler task
    ret = xTaskCreatePinnedToCore(
        event_handler_task,
        "event_task",
        EVENT_TASK_STACK_SIZE,
        ctx,
        EVENT_TASK_PRIORITY,
        &ctx->event_task_handle,
        0  // Pin to core 0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create event handler task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All tasks started successfully");

    return ESP_OK;
}

esp_err_t app_tasks_stop(app_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Stopping application tasks...");

    // Signal shutdown
    xEventGroupSetBits(ctx->system_events, EVENT_SHUTDOWN);

    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "All tasks stopped");

    return ESP_OK;
}

void app_tasks_cleanup(app_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    ESP_LOGI(TAG, "Cleaning up application tasks...");

    // Clean up queues
    if (ctx->raw_frame_queue) {
        vQueueDelete(ctx->raw_frame_queue);
    }
    if (ctx->encoded_frame_queue) {
        vQueueDelete(ctx->encoded_frame_queue);
    }
    if (ctx->event_queue) {
        vQueueDelete(ctx->event_queue);
    }

    // Clean up semaphores
    if (ctx->camera_mutex) {
        vSemaphoreDelete(ctx->camera_mutex);
    }
    if (ctx->encoder_mutex) {
        vSemaphoreDelete(ctx->encoder_mutex);
    }

    // Clean up event group
    if (ctx->system_events) {
        vEventGroupDelete(ctx->system_events);
    }

    ESP_LOGI(TAG, "Cleanup complete");
}
