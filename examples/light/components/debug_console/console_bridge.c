#if CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED

#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_console.h"
#include "debug_console.h"

#define RX_STREAM_BYTES   1024U
#define LINE_MAX_BYTES     256U
#define TX_CHUNK_MAX       244U

static const char *TAG = "dbg_bridge";

/* RX byte stream from GATT writes */
static StreamBufferHandle_t s_rx_stream = NULL;

/* Original vprintf we'll call after mirroring to BLE */
static int (*s_orig_vprintf)(const char *fmt, va_list) = NULL;

/* Mirror logs over BLE? (default OFF for safety) */
static volatile bool s_mirror_logs = false;

/* Forward declarations */
static void rx_line_task(void *arg);
static int  ble_mirror_vprintf(const char *fmt, va_list ap);

/* --- Public API --------------------------------------------------------- */

esp_err_t console_bridge_init(void)
{
    if (!s_rx_stream) {
        s_rx_stream = xStreamBufferCreate(RX_STREAM_BYTES, 1);
        ESP_RETURN_ON_FALSE(s_rx_stream != NULL, ESP_FAIL, TAG, "stream create failed");
    }

    /* Spawn the line assembler */
    BaseType_t ok = xTaskCreate(rx_line_task, "dbg_rx_line",
                                3072, NULL, tskIDLE_PRIORITY+2, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "rx_line_task create failed");

    /* Install vprintf hook (mirror logs to BLE, then to UART) */
    s_orig_vprintf = esp_log_set_vprintf(ble_mirror_vprintf);
    return ESP_OK;
}

void console_bridge_feed_rx(const uint8_t *data, size_t len)
{
    if (!s_rx_stream || !data || len == 0) return;
    (void)xStreamBufferSend(s_rx_stream, data, len, 0);
}

void console_bridge_set_log_mirror(bool enable) { s_mirror_logs = enable; }
bool console_bridge_get_log_mirror(void) { return s_mirror_logs; }

/* --- Internal helpers --------------------------------------------------- */

static inline void notify_chunked(const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t n = len - off;
        if (n > TX_CHUNK_MAX) n = TX_CHUNK_MAX;
        debug_console_notify(&buf[off], n);
        off += n;
        taskYIELD();
    }
}

static int ble_mirror_vprintf(const char *fmt, va_list ap)
{
    char tmp[256];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap_copy);
    va_end(ap_copy);

    if (n > 0 && s_mirror_logs) {
        if (debug_console_is_connected()) {
            size_t len = (size_t)n;
            if (len > sizeof(tmp)) len = sizeof(tmp);
            notify_chunked((const uint8_t *)tmp, len);
        }
    }

    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, ap);
    }
    return n;
}

static void rx_line_task(void *arg)
{
    (void)arg;
    uint8_t line[LINE_MAX_BYTES];
    size_t  cursor = 0;

    for (;;) {
        uint8_t ch;
        size_t got = xStreamBufferReceive(s_rx_stream, &ch, 1, portMAX_DELAY);
        if (got != 1) continue;

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n' || cursor >= (LINE_MAX_BYTES - 1)) {
            line[cursor] = 0;
            /* Trim trailing spaces */
            while (cursor > 0 && (line[cursor-1] == ' ' || line[cursor-1] == '\t')) {
                line[--cursor] = 0;
            }

            /* --- Simple commands: logs on/off, status, help, bond, rate --- */
            if (cursor > 0) {
                if (strcasecmp((const char*)line, "logs on") == 0) {
                    s_mirror_logs = true;
                    const char ok[] = "OK: logs ON\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else if (strcasecmp((const char*)line, "logs off") == 0) {
                    s_mirror_logs = false;
                    const char ok[] = "OK: logs OFF\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else if (strcasecmp((const char*)line, "status") == 0) {
                    char buf[128];
                    int n = snprintf(buf, sizeof(buf),
                                     "status: logs=%s, require_bond=%s, rate_limit=%s\r\n",
                                     s_mirror_logs?"on":"off",
                                     debug_console_get_require_bond()?"on":"off",
                                     debug_console_get_rate_limit()?"on":"off");
                    if (n > 0) debug_console_notify((const uint8_t*)buf, (size_t)n);
                } else if (strcasecmp((const char*)line, "help") == 0) {
                    const char help[] =
                        "Commands:\r\n"
                        "  help            - show this help\r\n"
                        "  status          - show console status\r\n"
                        "  logs on|off     - mirror ESP_LOG* to BLE\r\n"
                        "  bond strict on|off - require bonded peer (default ON)\r\n"
                        "  rate on|off     - enable/disable TX rate limiter (default ON)\r\n";
                    debug_console_notify((const uint8_t*)help, sizeof(help)-1);
                } else if (strcasecmp((const char*)line, "bond strict on") == 0) {
                    debug_console_set_require_bond(true);
                    const char ok[] = "OK: require_bond=ON\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else if (strcasecmp((const char*)line, "bond strict off") == 0) {
                    debug_console_set_require_bond(false);
                    const char ok[] = "OK: require_bond=OFF\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else if (strcasecmp((const char*)line, "rate on") == 0) {
                    debug_console_set_rate_limit(true);
                    const char ok[] = "OK: rate_limit=ON\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else if (strcasecmp((const char*)line, "rate off") == 0) {
                    debug_console_set_rate_limit(false);
                    const char ok[] = "OK: rate_limit=OFF\r\n";
                    debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                } else {
                    /* Try running as esp_console command */
                    int err;
                    esp_err_t rc = esp_console_run((const char*)line, &err);
                    if (rc == ESP_ERR_NOT_FOUND) {
                        /* Not a console command, echo back */
                        notify_chunked(line, cursor);
                        const char crlf[] = "\r\n";
                        debug_console_notify((const uint8_t *)crlf, 2);
                    } else if (rc == ESP_OK) {
                        /* Command ran successfully, output already went to vprintf */
                        const char ok[] = "OK\r\n";
                        debug_console_notify((const uint8_t*)ok, sizeof(ok)-1);
                    } else {
                        /* Command error */
                        char errbuf[64];
                        int n = snprintf(errbuf, sizeof(errbuf), "Error: %d\r\n", err);
                        if (n > 0) debug_console_notify((const uint8_t*)errbuf, (size_t)n);
                    }
                }
            } else {
                const char crlf[] = "\r\n";
                debug_console_notify((const uint8_t *)crlf, 2);
            }

            cursor = 0;
        } else {
            line[cursor++] = ch;
        }
    }
}

#endif /* CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED */
