/*
 * Sole owner of the HaLow module's AT UART (UART1). Mirrors the transport in
 * the thalow-config CLI/web page: write one line, collect until the reply goes
 * quiet. Serialised with an internal mutex so the HTTP handlers can share it.
 *
 * License: GPL 3.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t at_uart_init(void);

/*
 * Send `line` and collect the reply until it has been quiet for `quiet_ms`,
 * `max_ms` has elapsed, or `out` is full. Returns the number of bytes written
 * to `out` (NUL-terminated), or -1 if the port mutex could not be taken.
 */
int at_uart_cmd(const char *line, int quiet_ms, int max_ms, char *out, size_t out_sz);

/*
 * Recover a console stuck in AT+TXDATA data-mode, or one drowning in debug
 * output: feed filler bytes to satisfy any pending read, then confirm AT works.
 */
bool at_uart_resync(void);

/* Stop the periodic LMAC/WNB status dump so replies are parseable. */
void at_uart_quiet(void);

#ifdef __cplusplus
}
#endif
