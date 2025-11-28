/*
 * UVC Example Header - Exported structures and functions
 */

#ifndef UVC_EXAMPLE_H
#define UVC_EXAMPLE_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUFFER_COUNT 2

/* UVC context structure */
typedef struct uvc {
    int cap_fd;
    uint32_t format;
    uint8_t *cap_buffer[BUFFER_COUNT];

    int m2m_fd;
    uint8_t *m2m_cap_buffer;

    uvc_fb_t fb;
} uvc_t;

/* Initialization functions */
esp_err_t init_capture_video(uvc_t *uvc);
esp_err_t init_codec_video(uvc_t *uvc);
esp_err_t init_uvc_device(uvc_t *uvc);

#ifdef __cplusplus
}
#endif

#endif // UVC_EXAMPLE_H
