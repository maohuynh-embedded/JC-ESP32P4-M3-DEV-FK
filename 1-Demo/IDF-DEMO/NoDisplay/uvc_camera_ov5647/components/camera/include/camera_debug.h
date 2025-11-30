/*
 * Camera Debug Logger
 *
 * Provides debugging utilities for camera data analysis including:
 * - Hex dump of raw frame data
 * - Image format analysis (JPEG/H264 headers)
 * - Frame statistics (FPS, bitrate, size)
 * - Performance monitoring
 */

#ifndef CAMERA_DEBUG_H
#define CAMERA_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Debug levels */
typedef enum {
    CAM_DEBUG_NONE        = 0x00,
    CAM_DEBUG_STATS       = 0x01,  // Show frame statistics
    CAM_DEBUG_HEADER      = 0x02,  // Show image format headers
    CAM_DEBUG_HEX_HEADER  = 0x04,  // Hex dump of first bytes
    CAM_DEBUG_HEX_FULL    = 0x08,  // Full hex dump (memory intensive!)
    CAM_DEBUG_TIMING      = 0x10,  // Show timing information
    CAM_DEBUG_ALL         = 0xFF
} camera_debug_level_t;

/* Frame statistics */
typedef struct {
    uint32_t frame_count;
    uint32_t total_bytes;
    uint32_t min_size;
    uint32_t max_size;
    uint32_t avg_size;
    float fps;
    float bitrate_kbps;
    int64_t last_frame_time;
    uint32_t dropped_frames;
} camera_stats_t;

/* Image format types */
typedef enum {
    IMG_FORMAT_UNKNOWN = 0,
    IMG_FORMAT_JPEG,
    IMG_FORMAT_H264,
    IMG_FORMAT_RAW
} image_format_t;

/* Image header info */
typedef struct {
    image_format_t format;
    uint16_t width;
    uint16_t height;
    uint8_t quality;  // For JPEG
    bool has_soi;     // JPEG: Start of Image
    bool has_eoi;     // JPEG: End of Image
    bool has_sps;     // H264: Sequence Parameter Set
    bool has_pps;     // H264: Picture Parameter Set
    bool is_keyframe; // H264: IDR frame
} image_header_info_t;

/**
 * @brief Initialize camera debug module
 *
 * @param debug_level Combination of camera_debug_level_t flags
 * @return ESP_OK on success
 */
esp_err_t camera_debug_init(uint32_t debug_level);

/**
 * @brief Set debug level at runtime
 *
 * @param debug_level New debug level
 */
void camera_debug_set_level(uint32_t debug_level);

/**
 * @brief Get current debug level
 *
 * @return Current debug level flags
 */
uint32_t camera_debug_get_level(void);

/**
 * @brief Process and log frame data
 *
 * @param data Frame data buffer
 * @param len Frame data length
 * @param timestamp Frame timestamp (microseconds)
 * @return ESP_OK on success
 */
esp_err_t camera_debug_process_frame(const uint8_t *data, size_t len, int64_t timestamp);

/**
 * @brief Print hex dump of data
 *
 * @param data Data buffer
 * @param len Data length
 * @param bytes_per_line Bytes to display per line (default 16)
 */
void camera_debug_hex_dump(const uint8_t *data, size_t len, uint8_t bytes_per_line);

/**
 * @brief Analyze and print image format information
 *
 * @param data Frame data buffer
 * @param len Frame data length
 * @param info Output header information
 * @return ESP_OK if format detected
 */
esp_err_t camera_debug_analyze_format(const uint8_t *data, size_t len, image_header_info_t *info);

/**
 * @brief Get current statistics
 *
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t camera_debug_get_stats(camera_stats_t *stats);

/**
 * @brief Reset statistics counters
 */
void camera_debug_reset_stats(void);

/**
 * @brief Print current statistics
 */
void camera_debug_print_stats(void);

/**
 * @brief Print detailed frame information
 *
 * @param frame_num Frame number
 * @param data Frame data
 * @param len Frame length
 */
void camera_debug_print_frame_info(uint32_t frame_num, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_DEBUG_H
