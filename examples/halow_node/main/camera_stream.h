/*
 * Camera H.264 streaming for the HaLow node (T-Halow-P4 + CSI camera module).
 *
 * License: GPL 3.0
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"

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

/*
 * Register the browser front end on an already-running server:
 *   /camera        focus + range page, sized for a phone
 *   /preview.mjpg  MJPEG stream (?q=5..95), what an <img> can actually show
 *   /snapshot.jpg  single frame
 */
void camera_web_register(httpd_handle_t server);

/*
 * Live figures for /api/link. `frame_bytes` is the last encoded frame size,
 * which at fixed JPEG quality peaks when the lens is sharp -- the focus aid.
 */
void camera_stream_stats(bool *viewer, uint32_t *fps_x10, uint32_t *kbits,
                         uint32_t *frame_bytes);

/*
 * Master switch for the H.264 server (POST /api/camera enabled=0|1),
 * persisted in NVS. Off means the pipeline is never built, so a range test
 * measures the link and nothing else.
 */
bool camera_stream_enabled(void);
void camera_stream_set_enabled(bool enable);

/* Times the encoder could not start, usually a fragmented internal heap. */
uint32_t camera_stream_failures(void);

/* RTP-over-UDP push: connectionless, so a bad link costs picture, not the
 * session. Destination persisted; POST /api/udp host=&port=&enabled=. */
bool camera_udp_enabled(void);
const char *camera_udp_host(void);
uint16_t camera_udp_port(void);
void camera_udp_configure(const char *host, uint16_t port, bool enable);

#ifdef __cplusplus
}
#endif
