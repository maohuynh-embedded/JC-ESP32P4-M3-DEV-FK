/*
 * UVC App Common - Shared definitions and utilities
 */

#ifndef UVC_APP_COMMON_H
#define UVC_APP_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "usb_device_uvc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========= LOG TAG DEFINITIONS =========  */
#define CAM_TAG         "camera"
#define ENC_TAG         "encoding"
#define UVC_TAG         "uvc_stream"
#define MON_TAG         "monitor"
#define EVT_TAG         "event"
#define APP_TAG         "app_main"

/* ========= EVENT GROUP BITS ========= */
#define EVENT_CAMERA_READY      BIT0
#define EVENT_ENCODER_READY     BIT1
#define EVENT_UVC_READY         BIT2
#define EVENT_STREAMING_ACTIVE  BIT3
#define EVENT_SHUTDOWN          BIT7

/* ========= FRAME BUFFER STRUCTURE ========= */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int64_t timestamp;
    uint32_t frame_number;
    uint32_t format;  // V4L2_PIX_FMT_*

    /* For zero-copy: reference to camera buffer instead of PSRAM copy */
    int camera_buf_index;  // -1 if using PSRAM, >=0 if using camera mmap buffer
    bool is_camera_buffer; // true if data points to camera mmap buffer
} frame_buffer_t;

/* ========= SYSTEM EVENT TYPES ========= */
typedef enum {
    SYS_EVENT_START_STREAM,
    SYS_EVENT_STOP_STREAM,
    SYS_EVENT_RESET_STATS,
    SYS_EVENT_CHANGE_FORMAT,
    SYS_EVENT_CHANGE_RESOLUTION,
    SYS_EVENT_ERROR
} system_event_type_t;

typedef struct {
    system_event_type_t type;
    void *data;
    size_t data_len;
} system_event_t;

/* ========= UVC CONTEXT STRUCTURE ========= */
#define BUFFER_COUNT 2

typedef struct uvc {
    int cap_fd;
    uint32_t format;
    uint8_t *cap_buffer[BUFFER_COUNT];

    int m2m_fd;
    uint8_t *m2m_cap_buffer;
    uint8_t *m2m_out_buffer;        /* DMA-capable buffer for encoder input */
    size_t m2m_out_buffer_size;

    uvc_fb_t fb;
} uvc_t;

/* ========= GLOBAL APPLICATION CONTEXT ========= */
typedef struct {
    /* Synchronization primitives */
    EventGroupHandle_t system_events;
    SemaphoreHandle_t camera_mutex;
    SemaphoreHandle_t encoder_mutex;

    /* Shared resources */
    uvc_t *uvc;

    /* Statistics */
    bool is_streaming;
    uint32_t total_frames_captured;
    uint32_t total_frames_encoded;
    uint32_t total_frames_streamed;
    uint32_t frames_dropped;

} app_context_t;

/* ========= GLOBAL CONTEXT ACCESS ========= */
extern app_context_t g_app_ctx;

/* ========= FRAME BUFFER MANAGEMENT ========= */
frame_buffer_t *frame_buffer_alloc(size_t capacity);
void frame_buffer_free(frame_buffer_t *frame);
esp_err_t frame_buffer_resize(frame_buffer_t *frame, size_t new_capacity);

/* ========= EVENT POSTING ========= */
esp_err_t app_post_event(system_event_type_t type, void *data, size_t data_len);

/* ========= INITIALIZATION FUNCTIONS ========= */
void uvc_app_hw_init(void);
void uvc_app_debug_init(void);

/* ========= ERROR CHECKING MACROS ========= */
#define APP_RETURN_ON_ERROR(x, tag, format, ...) do {                         \
        esp_err_t err_rc_ = (x);                                               \
        if (unlikely(err_rc_ != ESP_OK)) {                                     \
            ESP_LOGE(tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            return err_rc_;                                                    \
        }                                                                      \
    } while(0)

#define APP_RETURN_ON_FALSE(a, err_code, tag, format, ...) do {               \
        if (unlikely(!(a))) {                                                 \
            ESP_LOGE(tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            return err_code;                                                   \
        }                                                                      \
    } while(0)

#define APP_LOG_ON_ERROR(x, log_tag, format, ...) do {                                          \
        esp_err_t err_rc_ = (x);                                                                \
        if (unlikely(err_rc_ != ESP_OK)) {                                                      \
            ESP_LOGE(log_tag, "%s(%d) - error %d : " format, __FUNCTION__, __LINE__, err_rc_, ##__VA_ARGS__);        \
        }                                                                                       \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* UVC_APP_COMMON_H */
