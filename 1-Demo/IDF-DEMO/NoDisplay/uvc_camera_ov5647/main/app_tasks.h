/*
 * FreeRTOS Multi-Task Application Architecture
 *
 * Kiến trúc:
 * - Camera Task: Capture frames từ camera
 * - Encoding Task: Encode frames (JPEG/H264)
 * - UVC Stream Task: Gửi data qua USB UVC
 * - Monitor Task: Theo dõi performance và statistics
 * - Event Task: Xử lý events và control commands
 */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "uvc_example.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task priorities (higher number = higher priority) */
#define CAMERA_TASK_PRIORITY        5
#define ENCODING_TASK_PRIORITY      4
#define UVC_STREAM_TASK_PRIORITY    3
#define MONITOR_TASK_PRIORITY       1
#define EVENT_TASK_PRIORITY         2

/* Task stack sizes */
#define CAMERA_TASK_STACK_SIZE      (4 * 1024)
#define ENCODING_TASK_STACK_SIZE    (8 * 1024)
#define UVC_STREAM_TASK_STACK_SIZE  (4 * 1024)
#define MONITOR_TASK_STACK_SIZE     (3 * 1024)
#define EVENT_TASK_STACK_SIZE       (2 * 1024)

/* Queue sizes */
#define FRAME_QUEUE_SIZE            3
#define ENCODED_QUEUE_SIZE          3
#define EVENT_QUEUE_SIZE            10

/* Event group bits */
#define EVENT_CAMERA_READY      BIT0
#define EVENT_ENCODER_READY     BIT1
#define EVENT_UVC_READY         BIT2
#define EVENT_STREAMING_ACTIVE  BIT3
#define EVENT_SHUTDOWN          BIT7

/* Frame buffer structure */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int64_t timestamp;
    uint32_t frame_number;
    uint32_t format;  // V4L2_PIX_FMT_*
} frame_buffer_t;

/* System event structure */
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

/* Application context */
typedef struct {
    // Task handles
    TaskHandle_t camera_task_handle;
    TaskHandle_t encoding_task_handle;
    TaskHandle_t uvc_stream_task_handle;
    TaskHandle_t monitor_task_handle;
    TaskHandle_t event_task_handle;

    // Communication primitives
    QueueHandle_t raw_frame_queue;      // Camera -> Encoding
    QueueHandle_t encoded_frame_queue;  // Encoding -> UVC Stream
    QueueHandle_t event_queue;          // System events

    SemaphoreHandle_t camera_mutex;
    SemaphoreHandle_t encoder_mutex;
    EventGroupHandle_t system_events;

    // Shared resources
    uvc_t *uvc;

    // State
    bool is_streaming;
    uint32_t total_frames_captured;
    uint32_t total_frames_encoded;
    uint32_t total_frames_streamed;
    uint32_t frames_dropped;

} app_context_t;

/* Task initialization */
esp_err_t app_tasks_init(app_context_t *ctx);
esp_err_t app_tasks_start(app_context_t *ctx);
esp_err_t app_tasks_stop(app_context_t *ctx);
void app_tasks_cleanup(app_context_t *ctx);

/* Frame buffer management */
frame_buffer_t *frame_buffer_alloc(size_t capacity);
void frame_buffer_free(frame_buffer_t *frame);
esp_err_t frame_buffer_resize(frame_buffer_t *frame, size_t new_capacity);

/* Event posting */
esp_err_t app_post_event(app_context_t *ctx, system_event_type_t type, void *data, size_t data_len);

/* Task entry points (internal) */
void camera_capture_task(void *pvParameters);
void encoding_task(void *pvParameters);
void uvc_stream_task(void *pvParameters);
void monitor_task(void *pvParameters);
void event_handler_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // APP_TASKS_H
