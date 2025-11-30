/*
 * Camera Debug Logger Implementation
 */

#include "camera_debug.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cam_debug";

/* Module state */
static struct {
    uint32_t debug_level;
    camera_stats_t stats;
    bool initialized;
    int64_t start_time;
} s_debug_ctx = {0};

/* JPEG markers */
#define JPEG_SOI    0xFFD8  // Start of Image
#define JPEG_EOI    0xFFD9  // End of Image
#define JPEG_SOF0   0xFFC0  // Start of Frame (Baseline)
#define JPEG_DQT    0xFFDB  // Define Quantization Table
#define JPEG_DHT    0xFFC4  // Define Huffman Table
#define JPEG_SOS    0xFFDA  // Start of Scan

/* H.264 NAL unit types */
#define H264_NAL_SLICE          1
#define H264_NAL_DPA            2
#define H264_NAL_DPB            3
#define H264_NAL_DPC            4
#define H264_NAL_IDR_SLICE      5  // Keyframe
#define H264_NAL_SEI            6
#define H264_NAL_SPS            7  // Sequence Parameter Set
#define H264_NAL_PPS            8  // Picture Parameter Set
#define H264_NAL_AUD            9  // Access Unit Delimiter

/* Helper macros */
#define DEBUG_ENABLED(flag) (s_debug_ctx.debug_level & (flag))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Forward declarations */
static void update_statistics(size_t frame_size, int64_t timestamp);
static bool is_jpeg_format(const uint8_t *data, size_t len);
static bool is_h264_format(const uint8_t *data, size_t len);
static esp_err_t analyze_jpeg_header(const uint8_t *data, size_t len, image_header_info_t *info);
static esp_err_t analyze_h264_header(const uint8_t *data, size_t len, image_header_info_t *info);
static const char *get_h264_nal_type_name(uint8_t nal_type);

/* ========== Public API Implementation ========== */

esp_err_t camera_debug_init(uint32_t debug_level)
{
    if (s_debug_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_debug_ctx, 0, sizeof(s_debug_ctx));
    s_debug_ctx.debug_level = debug_level;
    s_debug_ctx.start_time = esp_timer_get_time();
    s_debug_ctx.stats.min_size = UINT32_MAX;
    s_debug_ctx.initialized = true;

    ESP_LOGI(TAG, "Camera debug initialized with level: 0x%02lX", debug_level);

    return ESP_OK;
}

void camera_debug_set_level(uint32_t debug_level)
{
    s_debug_ctx.debug_level = debug_level;
    ESP_LOGI(TAG, "Debug level changed to: 0x%02lX", debug_level);
}

uint32_t camera_debug_get_level(void)
{
    return s_debug_ctx.debug_level;
}

esp_err_t camera_debug_process_frame(const uint8_t *data, size_t len, int64_t timestamp)
{
    if (!s_debug_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Update statistics
    update_statistics(len, timestamp);

    // Print statistics if enabled
    if (DEBUG_ENABLED(CAM_DEBUG_STATS)) {
        ESP_LOGI(TAG, "Frame #%lu: %zu bytes, FPS: %.2f, Bitrate: %.2f kbps",
                 s_debug_ctx.stats.frame_count, len,
                 s_debug_ctx.stats.fps, s_debug_ctx.stats.bitrate_kbps);
    }

    // Analyze format if enabled
    if (DEBUG_ENABLED(CAM_DEBUG_HEADER)) {
        image_header_info_t info;
        if (camera_debug_analyze_format(data, len, &info) == ESP_OK) {
            camera_debug_print_frame_info(s_debug_ctx.stats.frame_count, data, len);
        }
    }

    // Hex dump header
    if (DEBUG_ENABLED(CAM_DEBUG_HEX_HEADER)) {
        size_t dump_size = MIN(256, len);  // First 256 bytes
        ESP_LOGI(TAG, "=== Frame #%lu Header Hex Dump (%zu bytes) ===",
                 s_debug_ctx.stats.frame_count, dump_size);
        camera_debug_hex_dump(data, dump_size, 16);
    }

    // Full hex dump (use with caution!)
    if (DEBUG_ENABLED(CAM_DEBUG_HEX_FULL)) {
        ESP_LOGW(TAG, "=== Frame #%lu Full Hex Dump (%zu bytes) ===",
                 s_debug_ctx.stats.frame_count, len);
        camera_debug_hex_dump(data, len, 16);
    }

    // Timing information
    if (DEBUG_ENABLED(CAM_DEBUG_TIMING)) {
        static int64_t last_time = 0;
        int64_t delta = timestamp - last_time;
        ESP_LOGI(TAG, "Frame #%lu timing: ts=%lld us, delta=%lld us (%.2f ms)",
                 s_debug_ctx.stats.frame_count, timestamp, delta, delta / 1000.0);
        last_time = timestamp;
    }

    return ESP_OK;
}

void camera_debug_hex_dump(const uint8_t *data, size_t len, uint8_t bytes_per_line)
{
    if (!data || len == 0) {
        return;
    }

    if (bytes_per_line == 0) {
        bytes_per_line = 16;
    }

    char line_buf[256];
    char ascii_buf[64];

    for (size_t i = 0; i < len; i += bytes_per_line) {
        int line_pos = 0;
        int ascii_pos = 0;

        // Print offset
        line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos,
                            "%08zX: ", i);

        // Print hex bytes
        for (size_t j = 0; j < bytes_per_line; j++) {
            if (i + j < len) {
                line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos,
                                    "%02X ", data[i + j]);
                // Build ASCII representation
                char c = data[i + j];
                ascii_buf[ascii_pos++] = (c >= 32 && c <= 126) ? c : '.';
            } else {
                line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos,
                                    "   ");
            }
        }

        ascii_buf[ascii_pos] = '\0';

        // Print ASCII
        line_pos += snprintf(line_buf + line_pos, sizeof(line_buf) - line_pos,
                            " | %s", ascii_buf);

        ESP_LOGI(TAG, "%s", line_buf);
    }
}

esp_err_t camera_debug_analyze_format(const uint8_t *data, size_t len, image_header_info_t *info)
{
    if (!data || len < 4 || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(image_header_info_t));

    // Detect format
    if (is_jpeg_format(data, len)) {
        return analyze_jpeg_header(data, len, info);
    } else if (is_h264_format(data, len)) {
        return analyze_h264_header(data, len, info);
    } else {
        info->format = IMG_FORMAT_RAW;
        return ESP_OK;
    }
}

esp_err_t camera_debug_get_stats(camera_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_debug_ctx.stats, sizeof(camera_stats_t));
    return ESP_OK;
}

void camera_debug_reset_stats(void)
{
    memset(&s_debug_ctx.stats, 0, sizeof(camera_stats_t));
    s_debug_ctx.stats.min_size = UINT32_MAX;
    s_debug_ctx.start_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Statistics reset");
}

void camera_debug_print_stats(void)
{
    camera_stats_t *s = &s_debug_ctx.stats;

    ESP_LOGI(TAG, "========== Camera Statistics ==========");
    ESP_LOGI(TAG, "Total frames:   %lu", s->frame_count);
    ESP_LOGI(TAG, "Dropped frames: %lu", s->dropped_frames);
    ESP_LOGI(TAG, "Total bytes:    %lu (%.2f MB)", s->total_bytes, s->total_bytes / 1048576.0);
    ESP_LOGI(TAG, "Frame size:     min=%lu, max=%lu, avg=%lu bytes",
             s->min_size, s->max_size, s->avg_size);
    ESP_LOGI(TAG, "FPS:            %.2f", s->fps);
    ESP_LOGI(TAG, "Bitrate:        %.2f kbps", s->bitrate_kbps);
    ESP_LOGI(TAG, "=======================================");
}

void camera_debug_print_frame_info(uint32_t frame_num, const uint8_t *data, size_t len)
{
    image_header_info_t info;

    if (camera_debug_analyze_format(data, len, &info) != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "=== Frame #%lu Info ===", frame_num);
    ESP_LOGI(TAG, "Size: %zu bytes", len);

    switch (info.format) {
        case IMG_FORMAT_JPEG:
            ESP_LOGI(TAG, "Format: JPEG");
            ESP_LOGI(TAG, "Dimensions: %dx%d", info.width, info.height);
            ESP_LOGI(TAG, "Has SOI: %s, Has EOI: %s",
                     info.has_soi ? "Yes" : "No",
                     info.has_eoi ? "Yes" : "No");
            break;

        case IMG_FORMAT_H264:
            ESP_LOGI(TAG, "Format: H.264");
            ESP_LOGI(TAG, "Keyframe: %s", info.is_keyframe ? "Yes" : "No");
            ESP_LOGI(TAG, "Has SPS: %s, Has PPS: %s",
                     info.has_sps ? "Yes" : "No",
                     info.has_pps ? "Yes" : "No");
            if (info.width > 0 && info.height > 0) {
                ESP_LOGI(TAG, "Dimensions: %dx%d", info.width, info.height);
            }
            break;

        case IMG_FORMAT_RAW:
            ESP_LOGI(TAG, "Format: RAW/Unknown");
            break;

        default:
            ESP_LOGI(TAG, "Format: Unknown");
            break;
    }
}

/* ========== Private Helper Functions ========== */

static void update_statistics(size_t frame_size, int64_t timestamp)
{
    camera_stats_t *s = &s_debug_ctx.stats;

    s->frame_count++;
    s->total_bytes += frame_size;
    s->min_size = MIN(s->min_size, frame_size);
    s->max_size = MAX(s->max_size, frame_size);
    s->avg_size = s->total_bytes / s->frame_count;

    // Calculate FPS
    if (s->last_frame_time > 0) {
        int64_t delta = timestamp - s->last_frame_time;
        if (delta > 0) {
            float instant_fps = 1000000.0 / delta;  // Convert us to fps
            // Exponential moving average
            s->fps = s->fps * 0.9 + instant_fps * 0.1;
        }
    }
    s->last_frame_time = timestamp;

    // Calculate bitrate
    int64_t elapsed_time = timestamp - s_debug_ctx.start_time;
    if (elapsed_time > 0) {
        s->bitrate_kbps = (s->total_bytes * 8.0) / (elapsed_time / 1000.0);  // kbps
    }
}

static bool is_jpeg_format(const uint8_t *data, size_t len)
{
    if (len < 2) return false;
    return (data[0] == 0xFF && data[1] == 0xD8);  // JPEG SOI marker
}

static bool is_h264_format(const uint8_t *data, size_t len)
{
    if (len < 4) return false;
    // Check for H.264 start code: 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01
    return ((data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) ||
            (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01));
}

static esp_err_t analyze_jpeg_header(const uint8_t *data, size_t len, image_header_info_t *info)
{
    info->format = IMG_FORMAT_JPEG;

    // Check SOI
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        info->has_soi = true;
    }

    // Check EOI
    if (len >= 2 && data[len-2] == 0xFF && data[len-1] == 0xD9) {
        info->has_eoi = true;
    }

    // Parse JPEG markers to find SOF (Start of Frame)
    size_t pos = 2;  // Skip SOI
    while (pos + 4 < len) {
        if (data[pos] != 0xFF) {
            pos++;
            continue;
        }

        uint8_t marker = data[pos + 1];

        // SOF0 marker contains dimensions
        if (marker == 0xC0) {
            if (pos + 9 < len) {
                info->height = (data[pos + 5] << 8) | data[pos + 6];
                info->width = (data[pos + 7] << 8) | data[pos + 8];
            }
            break;
        }

        // Skip this marker
        if (pos + 3 < len) {
            uint16_t marker_len = (data[pos + 2] << 8) | data[pos + 3];
            pos += 2 + marker_len;
        } else {
            break;
        }
    }

    return ESP_OK;
}

static esp_err_t analyze_h264_header(const uint8_t *data, size_t len, image_header_info_t *info)
{
    info->format = IMG_FORMAT_H264;

    size_t pos = 0;

    // Parse NAL units
    while (pos < len) {
        // Find start code
        size_t start_code_len = 0;
        if (pos + 3 < len && data[pos] == 0x00 && data[pos+1] == 0x00 && data[pos+2] == 0x00 && data[pos+3] == 0x01) {
            start_code_len = 4;
        } else if (pos + 2 < len && data[pos] == 0x00 && data[pos+1] == 0x00 && data[pos+2] == 0x01) {
            start_code_len = 3;
        } else {
            pos++;
            continue;
        }

        pos += start_code_len;

        if (pos >= len) break;

        // Parse NAL unit header
        uint8_t nal_byte = data[pos];
        uint8_t nal_type = nal_byte & 0x1F;

        ESP_LOGD(TAG, "NAL unit type: %s (%d)", get_h264_nal_type_name(nal_type), nal_type);

        switch (nal_type) {
            case H264_NAL_SPS:
                info->has_sps = true;
                // TODO: Parse SPS for dimensions
                break;

            case H264_NAL_PPS:
                info->has_pps = true;
                break;

            case H264_NAL_IDR_SLICE:
                info->is_keyframe = true;
                break;

            default:
                break;
        }

        pos++;
    }

    return ESP_OK;
}

static const char *get_h264_nal_type_name(uint8_t nal_type)
{
    switch (nal_type) {
        case H264_NAL_SLICE:      return "SLICE";
        case H264_NAL_DPA:        return "DPA";
        case H264_NAL_DPB:        return "DPB";
        case H264_NAL_DPC:        return "DPC";
        case H264_NAL_IDR_SLICE:  return "IDR_SLICE (Keyframe)";
        case H264_NAL_SEI:        return "SEI";
        case H264_NAL_SPS:        return "SPS";
        case H264_NAL_PPS:        return "PPS";
        case H264_NAL_AUD:        return "AUD";
        default:                  return "UNKNOWN";
    }
}
