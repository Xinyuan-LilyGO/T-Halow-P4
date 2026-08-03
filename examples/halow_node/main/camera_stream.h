/*
 * Camera H.264 streaming for the HaLow node (T-Halow-P4 + CSI camera module).
 *
 * License: GPL 3.0
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Power the camera rails, probe the sensor and, if one answers, start a TCP
 * server on CONFIG_NODE_STREAM_PORT serving the raw H.264 Annex-B elementary
 * stream (one client at a time; the capture/encode pipeline only runs while a
 * client is connected). Watch with e.g.
 *
 *   ffplay -fflags nobuffer -f h264 tcp://<node>:8554
 *
 * Returns ESP_OK when the streamer is up, an error when no sensor was found
 * (a board without a camera module: normal, not fatal).
 */
esp_err_t camera_stream_start(void);

/* "absent" (no sensor), "idle" (listening, no pipeline held) or "streaming". */
const char *camera_stream_state(void);

#ifdef __cplusplus
}
#endif
