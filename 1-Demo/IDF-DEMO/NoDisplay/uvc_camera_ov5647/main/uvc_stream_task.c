/*
 * UVC Stream Task
 *
 * Responsibilities:
 * - Receive encoded frames from encoding task
 * - Stream frames via USB UVC to host PC
 * - Manage UVC device communication
 */

#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "uvc_app_common.h"
#include "os_interface.h"
#include "usb_device_uvc.h"
#include "uvc_frame_config.h"
#include "linux/videodev2.h"

/* Task context */
typedef struct {
    uint32_t streamed_count;
    bool uvc_initialized;
    uvc_fb_t current_fb;
} uvc_stream_task_ctx_t;

static uvc_stream_task_ctx_t s_uvc_ctx = {0};

/* UVC Callbacks - forward declarations */
static esp_err_t video_start_cb(uvc_format_t uvc_format, int width, int height, int rate, void *cb_ctx);
static void video_stop_cb(void *cb_ctx);
static uvc_fb_t *video_fb_get_cb(void *cb_ctx);
static void video_fb_return_cb(uvc_fb_t *fb, void *cb_ctx);

/* ========== Init Phase ========== */
void initUvcStreamTask(void *arg)
{
    int index = 0;
    uvc_device_config_t config;

    ESP_LOGI(UVC_TAG, "Initializing UVC stream task...");

    s_uvc_ctx.streamed_count = 0;
    s_uvc_ctx.uvc_initialized = false;

    /* Configure UVC device */
    config.start_cb     = video_start_cb;
    config.fb_get_cb    = video_fb_get_cb;
    config.fb_return_cb = video_fb_return_cb;
    config.stop_cb      = video_stop_cb;
    config.cb_ctx       = NULL;  // Not used in this implementation

    config.uvc_buffer_size = UVC_FRAMES_INFO[index][0].width * UVC_FRAMES_INFO[index][0].height;
    config.uvc_buffer = malloc(config.uvc_buffer_size);
    assert(config.uvc_buffer);

    ESP_LOGI(UVC_TAG, "Format List");
    ESP_LOGI(UVC_TAG, "\tFormat(1) = %s", g_app_ctx.uvc->format == V4L2_PIX_FMT_JPEG ? "MJPEG" : "H.264");
    ESP_LOGI(UVC_TAG, "Frame List");
    ESP_LOGI(UVC_TAG, "\tFrame(1) = %d * %d @%dfps",
             UVC_FRAMES_INFO[index][0].width,
             UVC_FRAMES_INFO[index][0].height,
             UVC_FRAMES_INFO[index][0].rate);

    /* Initialize UVC device */
    ESP_ERROR_CHECK(uvc_device_config(index, &config));
    ESP_ERROR_CHECK(uvc_device_init());

    /* Signal UVC is ready */
    xEventGroupSetBits(g_app_ctx.system_events, EVENT_UVC_READY);

    s_uvc_ctx.uvc_initialized = true;
    ESP_LOGI(UVC_TAG, "UVC stream task initialized");
}

/* ========== Main Loop ========== */
void mainUvcStreamTask(void *arg)
{
    QueueHandle_t encoded_frame_queue;

    ESP_LOGI(UVC_TAG, "UVC stream task started on core %d", xPortGetCoreID());

    /* Wait for UVC to be ready */
    xEventGroupWaitBits(g_app_ctx.system_events, EVENT_UVC_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* Get queue handle */
    encoded_frame_queue = os_getQueueHandler(QUEUE_ENCODED_FRAME);
    if (!encoded_frame_queue) {
        ESP_LOGE(UVC_TAG, "Failed to get encoded frame queue");
        goto exit;
    }

    ESP_LOGI(UVC_TAG, "Ready to stream via UVC");

    /* Main loop */
    while (1) {
        /* Check for shutdown */
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(UVC_TAG, "Shutdown requested");
            break;
        }

        /* Wait for encoded frame */
        frame_buffer_t *frame = NULL;
        if (xQueueReceive(encoded_frame_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        /*
         * Store frame for UVC callback
         * The actual streaming is handled by UVC callbacks (video_fb_get_cb)
         * called from UVC interrupt context
         */
        memcpy(&s_uvc_ctx.current_fb, &g_app_ctx.uvc->fb, sizeof(uvc_fb_t));

        /* Update statistics */
        g_app_ctx.total_frames_streamed++;
        s_uvc_ctx.streamed_count++;

        ESP_LOGD(UVC_TAG, "Streamed frame #%lu (%zu bytes)", frame->frame_number, frame->size);

        /* Free frame buffer */
        frame_buffer_free(frame);
    }

exit:
    ESP_LOGI(UVC_TAG, "UVC stream task exiting");
    vTaskDelete(NULL);
}

/* ========== Terminate Phase ========== */
void terUvcStreamTask(void *arg)
{
    ESP_LOGI(UVC_TAG, "Terminating UVC stream task...");

    /* Clear ready flag */
    xEventGroupClearBits(g_app_ctx.system_events, EVENT_UVC_READY);

    /* Cleanup */
    s_uvc_ctx.uvc_initialized = false;

    ESP_LOGI(UVC_TAG, "UVC stream task terminated, streamed %lu frames", s_uvc_ctx.streamed_count);
}

/* ========== UVC Callbacks ========== */

static esp_err_t video_start_cb(uvc_format_t uvc_format, int width, int height, int rate, void *cb_ctx)
{
    int type;
    struct v4l2_buffer buf;
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    uint32_t capture_fmt = 0;

    ESP_LOGI(UVC_TAG, "UVC start: %dx%d @%dfps", width, height, rate);

    /* Determine capture format */
    if (g_app_ctx.uvc->format == V4L2_PIX_FMT_JPEG) {
        const uint32_t jpeg_input_formats[] = {
            V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_YUV422P,
            V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_GREY
        };
        int fmt_index = 0;

        while (!capture_fmt && fmt_index < 4) {
            struct v4l2_fmtdesc fmtdesc = {
                .index = fmt_index++,
                .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            };

            if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
                for (int i = 0; i < 4; i++) {
                    if (jpeg_input_formats[i] == fmtdesc.pixelformat) {
                        capture_fmt = jpeg_input_formats[i];
                        break;
                    }
                }
            }
        }

        if (!capture_fmt) {
            ESP_LOGE(UVC_TAG, "No compatible JPEG input format");
            return ESP_ERR_NOT_SUPPORTED;
        }
    } else {
        capture_fmt = V4L2_PIX_FMT_YUV420;
    }

    /* Configure camera capture stream */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_REQBUFS, &req));

    for (int i = 0; i < BUFFER_COUNT; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QUERYBUF, &buf));

        g_app_ctx.uvc->cap_buffer[i] = (uint8_t *)mmap(NULL, buf.length,
                                                        PROT_READ | PROT_WRITE,
                                                        MAP_SHARED,
                                                        g_app_ctx.uvc->cap_fd,
                                                        buf.m.offset);
        assert(g_app_ctx.uvc->cap_buffer[i]);
        ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &buf));
    }

    /* Configure encoder streams */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_REQBUFS, &req));

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = g_app_ctx.uvc->format;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_S_FMT, &format));

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_REQBUFS, &req));

    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = 0;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QUERYBUF, &buf));

    g_app_ctx.uvc->m2m_cap_buffer = (uint8_t *)mmap(NULL, buf.length,
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_SHARED,
                                                     g_app_ctx.uvc->m2m_fd,
                                                     buf.m.offset);
    assert(g_app_ctx.uvc->m2m_cap_buffer);
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &buf));

    /* Start streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMON, &type));
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMON, &type));
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_ERROR_CHECK(ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_STREAMON, &type));

    /* Signal streaming is active */
    app_post_event(SYS_EVENT_START_STREAM, NULL, 0);

    return ESP_OK;
}

static void video_stop_cb(void *cb_ctx)
{
    int type;

    ESP_LOGI(UVC_TAG, "UVC stop");

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_STREAMOFF, &type);

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMOFF, &type);

    /* Signal streaming stopped */
    app_post_event(SYS_EVENT_STOP_STREAM, NULL, 0);
}

static uvc_fb_t *video_fb_get_cb(void *cb_ctx)
{
    /* Return current frame buffer */
    return &s_uvc_ctx.current_fb;
}

static void video_fb_return_cb(uvc_fb_t *fb, void *cb_ctx)
{
    /* Frame returned from UVC */
    ESP_LOGD(UVC_TAG, "UVC frame returned");
}
