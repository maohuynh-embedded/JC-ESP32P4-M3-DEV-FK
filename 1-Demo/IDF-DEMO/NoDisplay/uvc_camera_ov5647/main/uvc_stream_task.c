/*
 * UVC Stream Task
 *
 * Responsibilities:
 * - Capture raw frames from camera (in UVC callbacks)
 * - Encode frames using hardware encoder
 * - Stream encoded frames via USB UVC to host PC
 * - All processing happens synchronously in video_fb_get_cb()
 */

#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
    ESP_LOGI(UVC_TAG, "UVC stream task started on core %d", xPortGetCoreID());

    /* Wait for UVC to be ready */
    xEventGroupWaitBits(g_app_ctx.system_events, EVENT_UVC_READY,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(UVC_TAG, "UVC ready - all capture/encode/stream handled in callbacks");

    /* Main loop - just monitor for shutdown */
    while (1) {
        /* Check for shutdown */
        if (xEventGroupGetBits(g_app_ctx.system_events) & EVENT_SHUTDOWN) {
            ESP_LOGI(UVC_TAG, "Shutdown requested");
            break;
        }

        /* All work is done in UVC callbacks (video_fb_get_cb, video_fb_return_cb) */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

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

        ESP_LOGI(UVC_TAG, "Selected camera format: 0x%x", capture_fmt);
    } else {
        capture_fmt = V4L2_PIX_FMT_YUV420;
    }

    /* Configure camera capture stream */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGE(UVC_TAG, "Camera doesn't support %dx%d resolution (errno=%d: %s)",
                 width, height, errno, strerror(errno));
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(UVC_TAG, "Camera format set: %dx%d, format=0x%x",
             format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);

    /* Configure camera controls for better image quality */
    struct v4l2_control ctrl;

    /* Enable auto white balance */
    ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = 1;
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        ESP_LOGI(UVC_TAG, "Auto white balance enabled");
    }

    /* Enable auto exposure */
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_AUTO;  // Auto exposure
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        ESP_LOGI(UVC_TAG, "Auto exposure enabled");
    }

    /* Set saturation for better color */
    ctrl.id = V4L2_CID_SATURATION;
    ctrl.value = 64;  // Default saturation (range usually 0-128)
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        ESP_LOGI(UVC_TAG, "Saturation set to %d", ctrl.value);
    }

    /* Set contrast */
    ctrl.id = V4L2_CID_CONTRAST;
    ctrl.value = 32;  // Default contrast
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        ESP_LOGI(UVC_TAG, "Contrast set to %d", ctrl.value);
    }

    /* Set brightness */
    ctrl.id = V4L2_CID_BRIGHTNESS;
    ctrl.value = 0;  // Default brightness
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
        ESP_LOGI(UVC_TAG, "Brightness set to %d", ctrl.value);
    }

    memset(&req, 0, sizeof(req));
    req.count  = BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to request camera buffers (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        
        if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(UVC_TAG, "Failed to query camera buffer %d (errno=%d: %s)", 
                     i, errno, strerror(errno));
            return ESP_FAIL;
        }

        g_app_ctx.uvc->cap_buffer[i] = (uint8_t *)mmap(NULL, buf.length,
                                                        PROT_READ | PROT_WRITE,
                                                        MAP_SHARED,
                                                        g_app_ctx.uvc->cap_fd,
                                                        buf.m.offset);
        if (!g_app_ctx.uvc->cap_buffer[i]) {
            ESP_LOGE(UVC_TAG, "Failed to mmap camera buffer %d", i);
            return ESP_FAIL;
        }
        
        if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(UVC_TAG, "Failed to queue camera buffer %d (errno=%d: %s)", 
                     i, errno, strerror(errno));
            return ESP_FAIL;
        }
    }
    
    /* Start camera capture streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to start camera capture streaming (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(UVC_TAG, "Camera capture streaming started");

    /* Configure encoder streams */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = capture_fmt;
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGE(UVC_TAG, "Encoder doesn't support %dx%d INPUT format (errno=%d: %s)", 
                 width, height, errno, strerror(errno));
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(UVC_TAG, "Encoder INPUT format set: %dx%d", format.fmt.pix.width, format.fmt.pix.height);
    
    /* Calculate output buffer size from format */
    size_t out_buf_size = format.fmt.pix.sizeimage;
    if (out_buf_size == 0) {
        out_buf_size = width * height * 3 / 2;  /* Estimate for YUV */
    }
    
    /* For encoder INPUT buffer, we have two options:
     * 1. Allocate DMA buffer (limited by internal RAM reserved for DMA ~32KB)
     * 2. Use PSRAM buffer and let encoder's DMA handle it (may not work)
     * 
     * Since internal DMA pool is limited, we'll skip allocating intermediate buffer
     * and handle errors in encoding task instead.
     */
    g_app_ctx.uvc->m2m_out_buffer = NULL;
    g_app_ctx.uvc->m2m_out_buffer_size = 0;
    ESP_LOGI(UVC_TAG, "Encoder INPUT buffer strategy: Send PSRAM frame directly (no intermediate buffer)");

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to request encoder INPUT buffers (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = g_app_ctx.uvc->format;
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to set encoder OUTPUT format (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGI(UVC_TAG, "Encoder OUTPUT format set: %dx%d %s", 
             format.fmt.pix.width, format.fmt.pix.height,
             g_app_ctx.uvc->format == V4L2_PIX_FMT_JPEG ? "JPEG" : "H.264");

    memset(&req, 0, sizeof(req));
    req.count  = 1;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to request encoder OUTPUT buffers (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = 0;
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QUERYBUF, &buf) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to query encoder OUTPUT buffer (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }

    g_app_ctx.uvc->m2m_cap_buffer = (uint8_t *)mmap(NULL, buf.length,
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_SHARED,
                                                     g_app_ctx.uvc->m2m_fd,
                                                     buf.m.offset);
    if (!g_app_ctx.uvc->m2m_cap_buffer) {
        ESP_LOGE(UVC_TAG, "Failed to mmap encoder OUTPUT buffer");
        return ESP_FAIL;
    }
    
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &buf) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to queue initial encoder capture buffer (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGD(UVC_TAG, "Encoder capture buffer queued at startup");

    /* Start streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to start encoder capture streaming (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGD(UVC_TAG, "Encoder capture streaming started");
    
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(UVC_TAG, "Failed to start encoder output streaming (errno=%d: %s)", 
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    ESP_LOGD(UVC_TAG, "Encoder output streaming started");

    /* Signal streaming is active */
    app_post_event(SYS_EVENT_START_STREAM, NULL, 0);
    ESP_LOGI(UVC_TAG, "UVC streaming initialized successfully (format: 0x%x, %dx%d @ %d fps)", 
             g_app_ctx.uvc->format, width, height, rate);

    return ESP_OK;
}

static void video_stop_cb(void *cb_ctx)
{
    int type;
    int ret;

    ESP_LOGI(UVC_TAG, "UVC stop callback");

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_STREAMOFF, &type);
    if (ret != 0) {
        ESP_LOGW(UVC_TAG, "Failed to stop camera capture streaming (errno=%d: %s)", errno, strerror(errno));
    } else {
        ESP_LOGD(UVC_TAG, "Camera capture streaming stopped");
    }

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
    if (ret != 0) {
        ESP_LOGW(UVC_TAG, "Failed to stop encoder output streaming (errno=%d: %s)", errno, strerror(errno));
    } else {
        ESP_LOGD(UVC_TAG, "Encoder output streaming stopped");
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_STREAMOFF, &type);
    if (ret != 0) {
        ESP_LOGW(UVC_TAG, "Failed to stop encoder capture streaming (errno=%d: %s)", errno, strerror(errno));
    } else {
        ESP_LOGD(UVC_TAG, "Encoder capture streaming stopped");
    }

    /* Signal streaming stopped */
    app_post_event(SYS_EVENT_STOP_STREAM, NULL, 0);
    ESP_LOGI(UVC_TAG, "UVC streaming stopped");
}

static uvc_fb_t *video_fb_get_cb(void *cb_ctx)
{
    struct v4l2_buffer cam_buf, enc_in_buf, enc_out_buf;
    int ret;

    /* Step 1: Dequeue frame from camera */
    memset(&cam_buf, 0, sizeof(cam_buf));
    cam_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cam_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_DQBUF, &cam_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to dequeue camera frame (errno=%d: %s)", errno, strerror(errno));
        return NULL;
    }

    ESP_LOGD(UVC_TAG, "Camera frame captured: index=%d, size=%u bytes",
             cam_buf.index, cam_buf.bytesused);

    /* Update statistics */
    g_app_ctx.total_frames_captured++;

    /* Step 2: Queue camera frame to encoder INPUT */
    memset(&enc_in_buf, 0, sizeof(enc_in_buf));
    enc_in_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_in_buf.memory = V4L2_MEMORY_USERPTR;
    enc_in_buf.index = 0;
    enc_in_buf.m.userptr = (unsigned long)g_app_ctx.uvc->cap_buffer[cam_buf.index];
    enc_in_buf.length = cam_buf.bytesused;  // Length is the data size for USERPTR mode

    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &enc_in_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to queue encoder input (errno=%d: %s)", errno, strerror(errno));

        /* Return camera buffer */
        ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cam_buf);
        return NULL;
    }

    ESP_LOGD(UVC_TAG, "Encoder input queued: addr=%p, length=%u",
             (void*)enc_in_buf.m.userptr, enc_in_buf.length);

    /* Step 3: Dequeue encoded frame from encoder OUTPUT (this triggers encoding) */
    memset(&enc_out_buf, 0, sizeof(enc_out_buf));
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_out_buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &enc_out_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to dequeue encoder output (errno=%d: %s)", errno, strerror(errno));

        /* Return camera buffer */
        ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cam_buf);
        return NULL;
    }

    ESP_LOGD(UVC_TAG, "Encoded frame ready: size=%u bytes", enc_out_buf.bytesused);

    /* Step 4: Return camera buffer to camera queue */
    ret = ioctl(g_app_ctx.uvc->cap_fd, VIDIOC_QBUF, &cam_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to return camera buffer (errno=%d: %s)", errno, strerror(errno));
        /* Continue anyway - encoder output is ready */
    }

    /* Step 5: Dequeue encoder INPUT buffer (now that encoding is done) */
    memset(&enc_in_buf, 0, sizeof(enc_in_buf));
    enc_in_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_in_buf.memory = V4L2_MEMORY_USERPTR;

    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_DQBUF, &enc_in_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to dequeue encoder input (errno=%d: %s)", errno, strerror(errno));
        /* Not critical - encoder output is already available */
    }

    ESP_LOGD(UVC_TAG, "Encoder input buffer returned");

    /* Update statistics */
    g_app_ctx.total_frames_encoded++;

    /* Step 6: Prepare UVC frame buffer */
    g_app_ctx.uvc->fb.buf = g_app_ctx.uvc->m2m_cap_buffer;
    g_app_ctx.uvc->fb.len = enc_out_buf.bytesused;
    g_app_ctx.uvc->fb.timestamp.tv_sec = enc_out_buf.timestamp.tv_sec;
    g_app_ctx.uvc->fb.timestamp.tv_usec = enc_out_buf.timestamp.tv_usec;

    /* Update streaming statistics */
    g_app_ctx.total_frames_streamed++;
    s_uvc_ctx.streamed_count++;

    ESP_LOGD(UVC_TAG, "Returning encoded frame to UVC: %u bytes", g_app_ctx.uvc->fb.len);

    return &g_app_ctx.uvc->fb;
}

static void video_fb_return_cb(uvc_fb_t *fb, void *cb_ctx)
{
    struct v4l2_buffer enc_out_buf;
    int ret;

    /* Re-queue encoder output buffer for next frame */
    memset(&enc_out_buf, 0, sizeof(enc_out_buf));
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_out_buf.memory = V4L2_MEMORY_MMAP;
    enc_out_buf.index = 0;

    ret = ioctl(g_app_ctx.uvc->m2m_fd, VIDIOC_QBUF, &enc_out_buf);
    if (ret != 0) {
        ESP_LOGE(UVC_TAG, "Failed to re-queue encoder output buffer (errno=%d: %s)",
                 errno, strerror(errno));
    } else {
        ESP_LOGD(UVC_TAG, "Encoder output buffer re-queued");
    }
}
