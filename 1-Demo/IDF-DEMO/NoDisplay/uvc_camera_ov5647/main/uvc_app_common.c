/*
 * UVC App Common - Shared utilities implementation
 */

#include "uvc_app_common.h"
#include "os_interface.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "usb_device_uvc.h"
#include "uvc_frame_config.h"
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "linux/videodev2.h"

#ifdef CONFIG_CAMERA_DEBUG_ENABLE
#include "camera_debug.h"
#endif

static const char *TAG = "app_common";

/* ========= GLOBAL APPLICATION CONTEXT ========= */
app_context_t g_app_ctx = {0};

/* ========= CAMERA/ENCODER CONFIGURATION ========= */
#if CONFIG_EXAMPLE_CAM_SENSOR_MIPI_CSI
#define CAM_DEV_PATH        ESP_VIDEO_MIPI_CSI_DEVICE_NAME
#elif CONFIG_EXAMPLE_CAM_SENSOR_DVP
#define CAM_DEV_PATH        ESP_VIDEO_DVP_DEVICE_NAME
#endif

#if CONFIG_FORMAT_MJPEG_CAM1
#define ENCODE_DEV_PATH     ESP_VIDEO_JPEG_DEVICE_NAME
#define UVC_OUTPUT_FORMAT   V4L2_PIX_FMT_JPEG
#elif CONFIG_FORMAT_H264_CAM1
#define ENCODE_DEV_PATH     ESP_VIDEO_H264_DEVICE_NAME
#define UVC_OUTPUT_FORMAT   V4L2_PIX_FMT_H264
#endif

/* Camera configuration */
#if CONFIG_EXAMPLE_CAM_SENSOR_MIPI_CSI
static const esp_video_init_csi_config_t csi_config[] = {
    {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port      = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_PORT,
                .scl_pin   = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SCL_PIN,
                .sda_pin   = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_SDA_PIN,
            },
            .freq = CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_FREQ,
        },
        .reset_pin = CONFIG_EXAMPLE_MIPI_CSI_CAM_SENSOR_RESET_PIN,
        .pwdn_pin  = CONFIG_EXAMPLE_MIPI_CSI_CAM_SENSOR_PWDN_PIN,
    },
};
#endif

#if CONFIG_EXAMPLE_CAM_SENSOR_DVP
static const esp_video_init_dvp_config_t dvp_config[] = {
    {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port      = CONFIG_EXAMPLE_DVP_SCCB_I2C_PORT,
                .scl_pin   = CONFIG_EXAMPLE_DVP_SCCB_I2C_SCL_PIN,
                .sda_pin   = CONFIG_EXAMPLE_DVP_SCCB_I2C_SDA_PIN,
            },
            .freq      = CONFIG_EXAMPLE_DVP_SCCB_I2C_FREQ,
        },
        .reset_pin = CONFIG_EXAMPLE_DVP_CAM_SENSOR_RESET_PIN,
        .pwdn_pin  = CONFIG_EXAMPLE_DVP_CAM_SENSOR_PWDN_PIN,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                CONFIG_EXAMPLE_DVP_D0_PIN, CONFIG_EXAMPLE_DVP_D1_PIN,
                CONFIG_EXAMPLE_DVP_D2_PIN, CONFIG_EXAMPLE_DVP_D3_PIN,
                CONFIG_EXAMPLE_DVP_D4_PIN, CONFIG_EXAMPLE_DVP_D5_PIN,
                CONFIG_EXAMPLE_DVP_D6_PIN, CONFIG_EXAMPLE_DVP_D7_PIN,
            },
            .vsync_io = CONFIG_EXAMPLE_DVP_VSYNC_PIN,
            .de_io = CONFIG_EXAMPLE_DVP_DE_PIN,
            .pclk_io = CONFIG_EXAMPLE_DVP_PCLK_PIN,
            .xclk_io = CONFIG_EXAMPLE_DVP_XCLK_PIN,
        },
        .xclk_freq = CONFIG_EXAMPLE_DVP_XCLK_FREQ,
    },
};
#endif

static const esp_video_init_config_t cam_config = {
#if CONFIG_EXAMPLE_CAM_SENSOR_MIPI_CSI
    .csi = csi_config,
#endif
#if CONFIG_EXAMPLE_CAM_SENSOR_DVP
    .dvp = dvp_config,
#endif
};

/* ========= FRAME BUFFER MANAGEMENT ========= */

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

/* ========= EVENT POSTING ========= */

esp_err_t app_post_event(system_event_type_t type, void *data, size_t data_len)
{
    QueueHandle_t event_queue = os_getQueueHandler(QUEUE_SYSTEM_EVENT);
    if (!event_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    system_event_t event = {
        .type = type,
        .data = data,
        .data_len = data_len
    };

    if (xQueueSend(event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to post event type %d", type);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* ========= HARDWARE INITIALIZATION ========= */

static void print_video_device_info(const struct v4l2_capability *capability)
{
    ESP_LOGI(TAG, "version: %d.%d.%d", (uint16_t)(capability->version >> 16),
             (uint8_t)(capability->version >> 8),
             (uint8_t)capability->version);
    ESP_LOGI(TAG, "driver:  %s", capability->driver);
    ESP_LOGI(TAG, "card:    %s", capability->card);
    ESP_LOGI(TAG, "bus:     %s", capability->bus_info);
}

static esp_err_t init_capture_video(uvc_t *uvc)
{
    int fd;
    struct v4l2_capability capability;

    fd = open(CAM_DEV_PATH, O_RDONLY);
    APP_RETURN_ON_FALSE(fd >= 0, ESP_FAIL, TAG, "Failed to open camera device");

    APP_RETURN_ON_ERROR(ioctl(fd, VIDIOC_QUERYCAP, &capability), TAG, "VIDIOC_QUERYCAP failed");
    print_video_device_info(&capability);

    uvc->cap_fd = fd;
    return ESP_OK;
}

static esp_err_t init_codec_video(uvc_t *uvc)
{
    int fd;
    const char *devpath = ENCODE_DEV_PATH;
    struct v4l2_capability capability;
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    fd = open(devpath, O_RDONLY);
    APP_RETURN_ON_FALSE(fd >= 0, ESP_FAIL, TAG, "Failed to open encoder device");

    APP_RETURN_ON_ERROR(ioctl(fd, VIDIOC_QUERYCAP, &capability), TAG, "VIDIOC_QUERYCAP failed");
    print_video_device_info(&capability);

#if CONFIG_FORMAT_MJPEG_CAM1
    controls.ctrl_class = V4L2_CID_JPEG_CLASS;
    controls.count      = 1;
    controls.controls   = control;
    control[0].id       = V4L2_CID_JPEG_COMPRESSION_QUALITY;
    control[0].value    = CONFIG_EXAMPLE_JPEG_COMPRESSION_QUALITY;
    APP_LOG_ON_ERROR(ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls), TAG, "Failed to set JPEG quality");
#elif CONFIG_FORMAT_H264_CAM1
    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count      = 1;
    controls.controls   = control;

    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
    control[0].value    = CONFIG_EXAMPLE_H264_I_PERIOD;
    APP_LOG_ON_ERROR(ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls), TAG, "Failed to set H264 I-period");

    control[0].id       = V4L2_CID_MPEG_VIDEO_BITRATE;
    control[0].value    = CONFIG_EXAMPLE_H264_BITRATE;
    APP_LOG_ON_ERROR(ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls), TAG, "Failed to set H264 bitrate");

    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
    control[0].value    = CONFIG_EXAMPLE_H264_MIN_QP;
    APP_LOG_ON_ERROR(ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls), TAG, "Failed to set H264 min QP");

    control[0].id       = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
    control[0].value    = CONFIG_EXAMPLE_H264_MAX_QP;
    APP_LOG_ON_ERROR(ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls), TAG, "Failed to set H264 max QP");
#endif

    uvc->format = UVC_OUTPUT_FORMAT;
    uvc->m2m_fd = fd;

    return ESP_OK;
}

void uvc_app_hw_init(void)
{
    ESP_LOGI(TAG, "Initializing video hardware...");

    /* Allocate UVC context */
    g_app_ctx.uvc = calloc(1, sizeof(uvc_t));
    assert(g_app_ctx.uvc);

    /* Initialize video subsystem */
    ESP_ERROR_CHECK(esp_video_init(&cam_config));
    ESP_ERROR_CHECK(init_capture_video(g_app_ctx.uvc));
    ESP_ERROR_CHECK(init_codec_video(g_app_ctx.uvc));

    /* Create synchronization primitives */
    g_app_ctx.system_events = xEventGroupCreate();
    assert(g_app_ctx.system_events);

    g_app_ctx.camera_mutex = xSemaphoreCreateMutex();
    assert(g_app_ctx.camera_mutex);

    g_app_ctx.encoder_mutex = xSemaphoreCreateMutex();
    assert(g_app_ctx.encoder_mutex);

    ESP_LOGI(TAG, "Video hardware initialized");
}

void uvc_app_debug_init(void)
{
#ifdef CONFIG_CAMERA_DEBUG_ENABLE
    uint32_t debug_level = 0;

#ifdef CONFIG_CAMERA_DEBUG_STATS
    debug_level |= CAM_DEBUG_STATS;
#endif
#ifdef CONFIG_CAMERA_DEBUG_HEADER
    debug_level |= CAM_DEBUG_HEADER;
#endif
#ifdef CONFIG_CAMERA_DEBUG_HEX_HEADER
    debug_level |= CAM_DEBUG_HEX_HEADER;
#endif
#ifdef CONFIG_CAMERA_DEBUG_HEX_FULL
    debug_level |= CAM_DEBUG_HEX_FULL;
#endif
#ifdef CONFIG_CAMERA_DEBUG_TIMING
    debug_level |= CAM_DEBUG_TIMING;
#endif

    if (debug_level > 0) {
        ESP_ERROR_CHECK(camera_debug_init(debug_level));
        ESP_LOGI(TAG, "Camera debug enabled with level: 0x%02lX", debug_level);
    }
#endif
}
