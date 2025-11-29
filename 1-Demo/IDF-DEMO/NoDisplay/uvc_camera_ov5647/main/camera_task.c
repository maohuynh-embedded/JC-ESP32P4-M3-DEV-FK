/*
 * Camera Capture Task
 *
 * Responsibilities:
 * - Capture frames from camera sensor (OV5647)
 * - Manage camera device file descriptor
 * - Send raw frames to encoding queue
 */

#include <string.h>
#include <sys/ioctl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "linux/videodev2.h"
#include "uvc_app_common.h"
#include "os_interface.h"

/* Task context (local state) */
typedef struct {
    uint32_t frame_counter;
    bool initialized;
    uint32_t capture_fmt;  // Camera output format (from video_start_cb)
} camera_task_ctx_t;

static camera_task_ctx_t s_cam_ctx = {0};

/* ========== Init Phase ========== */
void initCameraTask(void *arg)
{
    ESP_LOGI(CAM_TAG, "Initializing camera task...");

    s_cam_ctx.frame_counter = 0;
    s_cam_ctx.initialized = false;

    /* Signal camera is ready */
    xEventGroupSetBits(g_app_ctx.system_events, EVENT_CAMERA_READY);

    s_cam_ctx.initialized = true;
    ESP_LOGI(CAM_TAG, "Camera task initialized");
}

/* ========== Main Loop ========== */
void mainCameraTask(void *arg)
{
    struct v4l2_buffer cap_buf;
    QueueHandle_t raw_frame_queue;

    ESP_LOGI(CAM_TAG, "Camera task started on core %d", xPortGetCoreID());

    /* Wait for camera to be ready */
    xEventGroupWaitBits(g_app_ctx.system_events, EVENT_CAMERA_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* Get queue handle */
    raw_frame_queue = os_getQueueHandler(QUEUE_RAW_FRAME);
    if (!raw_frame_queue) {
        ESP_LOGE(CAM_TAG, "Failed to get raw frame queue");
        goto exit;
    }

    ESP_LOGI(CAM_TAG, "Waiting for streaming to start...");

    /* Main loop */
    while (1) {
        /* Check for shutdown */
        EventBits_t bits = xEventGroupGetBits(g_app_ctx.system_events);
        if (bits & EVENT_SHUTDOWN) {
            ESP_LOGI(CAM_TAG, "Shutdown requested");
            break;
        }

        /* Wait for streaming to be active */
        if (!(bits & EVENT_STREAMING_ACTIVE)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Capture frame from camera - thread-safe with mutex */
        xSemaphoreTake(g_app_ctx.camera_mutex, portMAX_DELAY);

        memset(&cap_buf, 0, sizeof(cap_buf));
        cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cap_buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_DQBUF, &cap_buf) != 0) {
            xSemaphoreGive(g_app_ctx.camera_mutex);
            ESP_LOGD(CAM_TAG, "VIDIOC_DQBUF failed, retrying...");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /*
         * Zero-copy optimization: Instead of copying to PSRAM (which encoder can't DMA from),
         * send reference to camera mmap buffer directly.
         * Encoding task will QBUF this buffer back to camera after encoding.
         */
        frame_buffer_t *frame = (frame_buffer_t *)heap_caps_malloc(sizeof(frame_buffer_t), MALLOC_CAP_DEFAULT);
        if (!frame) {
            ESP_LOGE(CAM_TAG, "Failed to allocate frame metadata");
            ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cap_buf);
            xSemaphoreGive(g_app_ctx.camera_mutex);
            g_app_ctx.frames_dropped++;
            continue;
        }

        /* Reference camera mmap buffer (zero-copy) */
        frame->data = g_app_ctx.uvc->cap_buffer[cap_buf.index];
        frame->size = cap_buf.bytesused;
        frame->capacity = cap_buf.length;
        frame->timestamp = esp_timer_get_time();
        frame->frame_number = s_cam_ctx.frame_counter++;
        frame->format = 0;  // Will be set by encoder
        frame->camera_buf_index = cap_buf.index;
        frame->is_camera_buffer = true;

        /* DO NOT QBUF yet - encoding task will return it after encoding */
        xSemaphoreGive(g_app_ctx.camera_mutex);

        /* Update statistics */
        g_app_ctx.total_frames_captured++;

        /* Send to encoding queue */
        if (xQueueSend(raw_frame_queue, &frame, 0) != pdTRUE) {
            ESP_LOGW(CAM_TAG, "Raw frame queue full, dropping frame #%lu", frame->frame_number);

            /* Must return camera buffer if queue full! */
            if (frame->is_camera_buffer) {
                struct v4l2_buffer cam_buf;
                memset(&cam_buf, 0, sizeof(cam_buf));
                cam_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                cam_buf.memory = V4L2_MEMORY_MMAP;
                cam_buf.index = frame->camera_buf_index;
                xSemaphoreTake(g_app_ctx.camera_mutex, portMAX_DELAY);
                ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cam_buf);
                xSemaphoreGive(g_app_ctx.camera_mutex);
                free(frame);
            } else {
                frame_buffer_free(frame);
            }
            g_app_ctx.frames_dropped++;
        } else {
            ESP_LOGI(CAM_TAG, "Sent frame #%lu to encoding (buf_idx=%d, %zu bytes)",
                     frame->frame_number, frame->camera_buf_index, frame->size);
        }

        /* Small yield to prevent CPU hogging */
        vTaskDelay(pdMS_TO_TICKS(1));
    }

exit:
    ESP_LOGI(CAM_TAG, "Camera task exiting");
    vTaskDelete(NULL);
}

/* ========== Terminate Phase ========== */
void terCameraTask(void *arg)
{
    ESP_LOGI(CAM_TAG, "Terminating camera task...");

    /* Clear ready flag */
    xEventGroupClearBits(g_app_ctx.system_events, EVENT_CAMERA_READY);

    /* Cleanup local resources */
    s_cam_ctx.initialized = false;

    ESP_LOGI(CAM_TAG, "Camera task terminated, captured %lu frames", s_cam_ctx.frame_counter);
}
