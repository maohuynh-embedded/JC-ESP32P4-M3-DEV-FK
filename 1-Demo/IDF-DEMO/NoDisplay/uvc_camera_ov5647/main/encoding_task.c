/*
 * Encoding Task
 *
 * Responsibilities:
 * - Receive raw frames from camera task
 * - Encode frames to JPEG or H.264
 * - Send encoded frames to UVC stream task
 */

#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "linux/videodev2.h"
#include "uvc_app_common.h"
#include "os_interface.h"

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
#include "camera_debug.h"
#endif

/* Task context */
typedef struct {
    uint32_t encoded_count;
    bool initialized;
} encoding_task_ctx_t;

static encoding_task_ctx_t s_enc_ctx = {0};

/* Helper: Return camera buffer if zero-copy frame */
static void return_camera_buffer_if_needed(frame_buffer_t *frame)
{
    if (frame->is_camera_buffer) {
        struct v4l2_buffer cam_buf;
        memset(&cam_buf, 0, sizeof(cam_buf));
        cam_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cam_buf.memory = V4L2_MEMORY_MMAP;
        cam_buf.index = frame->camera_buf_index;

        xSemaphoreTake(g_app_ctx.camera_mutex, portMAX_DELAY);
        if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cam_buf) != 0) {
            ESP_LOGE(ENC_TAG, "Failed to return camera buffer %d (errno=%d: %s)",
                     frame->camera_buf_index, errno, strerror(errno));
        } else {
            ESP_LOGD(ENC_TAG, "Returned camera buffer %d to driver", frame->camera_buf_index);
        }
        xSemaphoreGive(g_app_ctx.camera_mutex);
        free(frame);
    } else {
        frame_buffer_free(frame);
    }
}

/* ========== Init Phase ========== */
void initEncodingTask(void *arg)
{
    ESP_LOGI(ENC_TAG, "Initializing encoding task...");

    s_enc_ctx.encoded_count = 0;
    s_enc_ctx.initialized = false;

    /* Signal encoder is ready */
    xEventGroupSetBits(g_app_ctx.system_events, EVENT_ENCODER_READY);

    s_enc_ctx.initialized = true;
    ESP_LOGI(ENC_TAG, "Encoding task initialized");
}

/* ========== Main Loop ========== */
void mainEncodingTask(void *arg)
{
    struct v4l2_buffer m2m_out_buf;
    struct v4l2_buffer m2m_cap_buf;
    struct v4l2_format format;
    QueueHandle_t raw_frame_queue;
    QueueHandle_t encoded_frame_queue;

    ESP_LOGI(ENC_TAG, "Encoding task started on core %d", xPortGetCoreID());

    /* Wait for encoder to be ready */
    xEventGroupWaitBits(g_app_ctx.system_events, EVENT_ENCODER_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* Get queue handles */
    raw_frame_queue = os_getQueueHandler(QUEUE_RAW_FRAME);
    encoded_frame_queue = os_getQueueHandler(QUEUE_ENCODED_FRAME);

    if (!raw_frame_queue || !encoded_frame_queue) {
        ESP_LOGE(ENC_TAG, "Failed to get queue handles");
        goto exit;
    }

    /* Main loop */
    while (1) {
        EventBits_t bits;

        /* Check for shutdown */
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(ENC_TAG, "Shutdown requested");
            break;
        }

        /* Wait for streaming to be active */
        bits = xEventGroupGetBits(g_app_ctx.system_events);
        if (!(bits & EVENT_STREAMING_ACTIVE)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Wait for raw frame */
        frame_buffer_t *raw_frame = NULL;
        if (xQueueReceive(raw_frame_queue, &raw_frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        ESP_LOGI(ENC_TAG, "Received frame #%u, is_camera_buf=%d, index=%d, size=%zu",
                 raw_frame->frame_number, raw_frame->is_camera_buffer,
                 raw_frame->camera_buf_index, raw_frame->size);

        /* Encode frame - thread-safe with mutex */
        xSemaphoreTake(g_app_ctx.encoder_mutex, portMAX_DELAY);

        /* Send raw frame to encoder input
         * Note: Frame is in PSRAM. If encoder doesn't support PSRAM DMA,
         * it will reject with errno=22 (Invalid argument)
         */
        memset(&m2m_out_buf, 0, sizeof(m2m_out_buf));
        m2m_out_buf.index  = 0;
        m2m_out_buf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        m2m_out_buf.memory = V4L2_MEMORY_USERPTR;
        m2m_out_buf.m.userptr = (unsigned long)raw_frame->data;
        m2m_out_buf.length = raw_frame->size;

        ESP_LOGI(ENC_TAG, "QBUF input: addr=%p, size=%zu",
                 raw_frame->data, raw_frame->size);

        if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &m2m_out_buf) != 0) {
            ESP_LOGE(ENC_TAG, "Failed to queue encoder input buffer (errno=%d: %s)",
                     errno, strerror(errno));
            return_camera_buffer_if_needed(raw_frame);
            xSemaphoreGive(g_app_ctx.encoder_mutex);
            g_app_ctx.frames_dropped++;
            continue;
        }
        ESP_LOGI(ENC_TAG, "Encoder input buffer queued successfully");

        /* Get encoded frame from encoder */
        memset(&m2m_cap_buf, 0, sizeof(m2m_cap_buf));
        m2m_cap_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        m2m_cap_buf.memory = V4L2_MEMORY_MMAP;

        ESP_LOGI(ENC_TAG, "Calling DQBUF to get encoded frame...");
        if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &m2m_cap_buf) != 0) {
            ESP_LOGW(ENC_TAG, "Failed to dequeue encoder capture buffer (errno=%d: %s)", errno, strerror(errno));
            ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf);  // Try to cleanup
            return_camera_buffer_if_needed(raw_frame);
            xSemaphoreGive(g_app_ctx.encoder_mutex);
            continue;
        }
        ESP_LOGD(ENC_TAG, "Encoded frame received: %zu bytes, flags=0x%x", 
                 m2m_cap_buf.bytesused, m2m_cap_buf.flags);

        /* Allocate encoded frame buffer */
        frame_buffer_t *encoded_frame = frame_buffer_alloc(m2m_cap_buf.bytesused);
        if (!encoded_frame) {
            ESP_LOGE(ENC_TAG, "Failed to allocate encoded frame buffer (%zu bytes)", m2m_cap_buf.bytesused);
            ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &m2m_cap_buf);
            ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf);
            return_camera_buffer_if_needed(raw_frame);
            xSemaphoreGive(g_app_ctx.encoder_mutex);
            g_app_ctx.frames_dropped++;
            continue;
        }

        /* Copy encoded data */
        memcpy(encoded_frame->data, g_app_ctx.uvc->m2m_cap_buffer, m2m_cap_buf.bytesused);
        encoded_frame->size = m2m_cap_buf.bytesused;
        encoded_frame->timestamp = raw_frame->timestamp;
        encoded_frame->frame_number = raw_frame->frame_number;

        /* Get format info */
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_G_FMT, &format);
        encoded_frame->format = format.fmt.pix.pixelformat;

        /* Return buffers to encoder */
        if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &m2m_cap_buf) != 0) {
            ESP_LOGE(ENC_TAG, "Failed to requeue encoder capture buffer (errno=%d: %s)", errno, strerror(errno));
        } else {
            ESP_LOGD(ENC_TAG, "Encoder capture buffer requeued");
        }
        if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &m2m_out_buf) != 0) {
            ESP_LOGE(ENC_TAG, "Failed to dequeue encoder output buffer after encoding (errno=%d: %s)", errno, strerror(errno));
        } else {
            ESP_LOGD(ENC_TAG, "Encoder output buffer dequeued");
        }

        xSemaphoreGive(g_app_ctx.encoder_mutex);

        /* Return camera buffer to driver (using helper function) */
        return_camera_buffer_if_needed(raw_frame);

        /* Update statistics */
        g_app_ctx.total_frames_encoded++;
        s_enc_ctx.encoded_count++;

        ESP_LOGD(ENC_TAG, "Frame #%lu encoded: %zu bytes (seq: %u)", 
                 encoded_frame->frame_number, encoded_frame->size, encoded_frame->frame_number);

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
        /* Debug encoded frame */
        camera_debug_process_frame(encoded_frame->data, encoded_frame->size, encoded_frame->timestamp);
#endif

        /* Send to UVC stream queue */
        if (xQueueSend(encoded_frame_queue, &encoded_frame, 0) != pdTRUE) {
            ESP_LOGW(ENC_TAG, "Encoded frame queue full, dropping frame #%lu", encoded_frame->frame_number);
            frame_buffer_free(encoded_frame);
            g_app_ctx.frames_dropped++;
        }
    }

exit:
    ESP_LOGI(ENC_TAG, "Encoding task exiting");
    vTaskDelete(NULL);
}

/* ========== Terminate Phase ========== */
void terEncodingTask(void *arg)
{
    ESP_LOGI(ENC_TAG, "Terminating encoding task...");

    /* Clear ready flag */
    xEventGroupClearBits(g_app_ctx.system_events, EVENT_ENCODER_READY);

    /* Cleanup */
    s_enc_ctx.initialized = false;

    ESP_LOGI(ENC_TAG, "Encoding task terminated, encoded %lu frames", s_enc_ctx.encoded_count);
}
