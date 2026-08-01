/*
 * License: GPL 3.0
 */
#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"

#include "at_uart.h"
#include "web_server.h"

static const char *TAG = "web_server";

#define AT_LINE_MAX 128
#define AT_REPLY_MAX 4096
#define AT_QUIET_DEFAULT 250
#define AT_MAX_DEFAULT 1800

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static esp_err_t page_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start,
                           index_html_gz_end - index_html_gz_start);
}

/* Pull an integer query parameter, clamped to [lo, hi]; `fallback` if absent. */
static int query_int(httpd_req_t *req, const char *key, int fallback, int lo, int hi)
{
    char query[64];
    char value[16];
    int out = fallback;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK)
    {
        out = atoi(value);
    }
    if (out < lo)
    {
        out = lo;
    }
    if (out > hi)
    {
        out = hi;
    }
    return out;
}

/*
 * The radio persists ssid/channel/bandwidth/key itself but reverts its role on
 * every power-up, so the firmware must re-assert one at boot. When the config
 * page sets the role (AT+MODE=ap / AT+MODE=sta), remember it in NVS; app_main
 * asserts the remembered role instead of a hard-coded "sta".
 */
#define ROLE_NVS_NS "router"
#define ROLE_NVS_KEY "halow_role"

void web_server_get_halow_role(char *role, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(ROLE_NVS_NS, NVS_READONLY, &h) == ESP_OK)
    {
        size_t len = cap;
        nvs_get_str(h, ROLE_NVS_KEY, role, &len); /* leaves the default on miss */
        nvs_close(h);
    }
}

static void remember_halow_role(const char *line)
{
    if (strncasecmp(line, "AT+MODE=", 8) != 0)
    {
        return;
    }
    const char *mode = line + 8;
    if (strcasecmp(mode, "ap") != 0 && strcasecmp(mode, "sta") != 0)
    {
        return;
    }

    nvs_handle_t h;
    if (nvs_open(ROLE_NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_str(h, ROLE_NVS_KEY, mode);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "halow role \"%s\" saved, re-asserted on every boot", mode);
    }
}

static esp_err_t at_post_handler(httpd_req_t *req)
{
    char line[AT_LINE_MAX + 1];

    if (req->content_len == 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty AT line");
    }
    if (req->content_len > AT_LINE_MAX)
    {
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "AT line too long");
    }

    int received = httpd_req_recv(req, line, req->content_len);
    if (received <= 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read failed");
    }
    line[received] = '\0';

    int quiet_ms = query_int(req, "quiet", AT_QUIET_DEFAULT, 50, 1000);
    int max_ms = query_int(req, "max", AT_MAX_DEFAULT, 100, 8000);

    char *reply = malloc(AT_REPLY_MAX);
    if (reply == NULL)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    int len = at_uart_cmd(line, quiet_ms, max_ms, reply, AT_REPLY_MAX);
    if (len < 0)
    {
        free(reply);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AT port busy");
    }

    remember_halow_role(line);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, reply, len);
    free(reply);
    return err;
}

/*
 * Reboot the ESP32-P4 (not the radio). The board runs on battery, so pulling
 * USB power does not reset it and RST is a physical button -- this is the only
 * remote way back to a fresh boot (e.g. to apply a saved role without hands on
 * the board).
 */
static esp_err_t reboot_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, "OK, rebooting", HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "reboot requested via /api/reboot");
    vTaskDelay(pdMS_TO_TICKS(500)); /* let the response reach the client */
    esp_restart();
    return ESP_OK;                  /* not reached */
}

/*
 * Firmware update: POST the raw app image (build/halow_router.bin) to
 * /api/ota, e.g. curl --data-binary @halow_router.bin http://192.168.4.1/api/ota
 * Written to the inactive OTA slot, validated by esp_ota_end(), then booted.
 * With bootloader rollback enabled, an image that crashes before app_main
 * marks itself valid reverts to this one on the next reset.
 */
#define OTA_BUF_SIZE 4096

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (dst == NULL)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
    }
    if (req->content_len == 0)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    if (req->content_len > dst->size)
    {
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "image larger than OTA slot");
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (buf == NULL)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(dst, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK)
    {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "ota: %u bytes -> %s", (unsigned)req->content_len, dst->label);

    size_t remaining = req->content_len;
    while (remaining > 0 && err == ESP_OK)
    {
        int n = httpd_req_recv(req, buf, remaining < OTA_BUF_SIZE ? remaining : OTA_BUF_SIZE);
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }
        if (n <= 0)
        {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota, buf, n);
        remaining -= n;
    }
    free(buf);

    if (err == ESP_OK)
    {
        err = esp_ota_end(ota); /* verifies the complete image */
    }
    else
    {
        esp_ota_abort(ota);
    }
    if (err == ESP_OK)
    {
        err = esp_ota_set_boot_partition(dst);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ota failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, "OK, flashed; rebooting into new image", HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "ota complete, rebooting into %s", dst->label);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; /* not reached */
}

static esp_err_t resync_post_handler(httpd_req_t *req)
{
    bool ok = at_uart_resync();
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, ok ? "OK" : "FAIL", HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_get_handler};
    const httpd_uri_t index_html = {.uri = "/index.html", .method = HTTP_GET, .handler = page_get_handler};
    const httpd_uri_t api_at = {.uri = "/api/at", .method = HTTP_POST, .handler = at_post_handler};
    const httpd_uri_t api_resync = {.uri = "/api/resync", .method = HTTP_POST, .handler = resync_post_handler};
    const httpd_uri_t api_reboot = {.uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_post_handler};
    const httpd_uri_t api_ota = {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler};

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &index_html);
    httpd_register_uri_handler(server, &api_at);
    httpd_register_uri_handler(server, &api_resync);
    httpd_register_uri_handler(server, &api_reboot);
    httpd_register_uri_handler(server, &api_ota);

    ESP_LOGI(TAG, "config page up on http://192.168.4.1/");
    return ESP_OK;
}
