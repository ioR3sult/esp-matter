#include "console_bridge.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <lib/shell/Engine.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

extern "C" void debug_console_notify(const uint8_t *data, size_t len);
extern "C" void register_console_selftest(void);

static const char *TAG = "console_bridge";
static SemaphoreHandle_t s_console_mutex = NULL;

#define BLE_LINE_MAX 256
#define BLE_MAX_TOKENS 10

static char s_ble_line[BLE_LINE_MAX];
static size_t s_ble_len = 0;
static bool s_log_mirror_enabled = false;

static void ble_write(const uint8_t *data, size_t len) {
    debug_console_notify(data, len);
}

static void ble_echo(uint8_t c) {
    ble_write(&c, 1);
}

static void ble_crlf(void) {
#if CONFIG_DEBUG_CONSOLE_GATT_APPEND_CRLF
    static const char crlf[] = "\r\n";
    ble_write((const uint8_t*)crlf, 2);
#else
    uint8_t nl = '\n';
    ble_write(&nl, 1);
#endif
}

static void ble_prompt(void) {
    static const char p[] = "> ";
    ble_write((const uint8_t*)p, sizeof(p)-1);
}

static bool IsSeparator(char ch) {
    return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
}

static bool IsEscape(char ch) {
    return (ch == '\\');
}

static bool IsEscapable(char ch) {
    return IsSeparator(ch) || IsEscape(ch);
}

static int TokenizeLine(char *buffer, char **tokens, int max_tokens) {
    size_t len = strlen(buffer);
    int cursor = 0;
    size_t i = 0;

    while (buffer[i] && buffer[i] == ' ') {
        i++;
    }

    if (len <= i) {
        return 0;
    }

    tokens[cursor++] = &buffer[i];

    for (; i < len && cursor < max_tokens; i++) {
        if (IsEscape(buffer[i]) && IsEscapable(buffer[i + 1])) {
            memmove(&buffer[i], &buffer[i + 1], strlen(&buffer[i]));
        } else if (IsSeparator(buffer[i])) {
            buffer[i] = 0;
            if (!IsSeparator(buffer[i + 1])) {
                tokens[cursor++] = &buffer[i + 1];
            }
        }
    }

    if (cursor >= max_tokens) {
        cursor = max_tokens - 1;
    }

    tokens[cursor] = NULL;
    return cursor;
}

extern "C" esp_err_t console_bridge_init(void) {
    if (s_console_mutex) {
        ESP_LOGW(TAG, "Bridge already initialized");
        return ESP_OK;
    }

    s_console_mutex = xSemaphoreCreateMutex();
    if (!s_console_mutex) {
        ESP_LOGE(TAG, "Failed to create console mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Console bridge initialized (CHIP shell already running)");
    
    register_console_selftest();
    
    return ESP_OK;
}

extern "C" void console_bridge_set_log_mirror(bool enable) {
    s_log_mirror_enabled = enable;
    ESP_LOGI(TAG, "Log mirror %s", enable ? "enabled" : "disabled");
}

extern "C" void ble_console_on_subscribed(void) {
    ble_crlf();
    ble_prompt();
}

extern "C" void console_bridge_feed_rx(const uint8_t *data, size_t len) {
#if !CONFIG_DEBUG_CONSOLE_GATT_INPUT
    static bool warned = false;
    if (!warned) {
        ESP_LOGW(TAG, "BLE console input disabled (CONFIG_DEBUG_CONSOLE_GATT_INPUT=n)");
        warned = true;
    }
    return;
#endif

    for (size_t i = 0; i < len; ++i) {
        uint8_t c = data[i];

        if (c == '\r' || c == '\n') {
            if (s_ble_len == 0) {
                ble_crlf();
                ble_prompt();
                continue;
            }

            s_ble_line[s_ble_len] = 0;

            char *argv[BLE_MAX_TOKENS];
            int argc = TokenizeLine(s_ble_line, argv, BLE_MAX_TOKENS);

            if (argc > 0) {
                xSemaphoreTake(s_console_mutex, portMAX_DELAY);
                ble_crlf();
                
                CHIP_ERROR retval = chip::Shell::Engine::Root().ExecCommand(argc, argv);
                
                if (retval != CHIP_NO_ERROR) {
                    char err_msg[80];
                    snprintf(err_msg, sizeof(err_msg), "Error %s\r\n", argv[0]);
                    ble_write((const uint8_t*)err_msg, strlen(err_msg));
                } else {
                    static const char done[] = "Done\r\n";
                    ble_write((const uint8_t*)done, sizeof(done)-1);
                }
                
                xSemaphoreGive(s_console_mutex);
            } else {
                ble_crlf();
            }

            s_ble_len = 0;
            ble_prompt();
            continue;
        }

        if (c == 0x08 || c == 0x7F) {
            if (s_ble_len) {
                s_ble_len--;
                static const char bs[] = "\b \b";
                ble_write((const uint8_t*)bs, 3);
            }
            continue;
        }

        if (c >= 0x20 && c < 0x7F) {
            if (s_ble_len < BLE_LINE_MAX - 1) {
                s_ble_line[s_ble_len++] = (char)c;
                ble_echo(c);
            } else {
                uint8_t bell = 0x07;
                ble_write(&bell, 1);
            }
        }
    }
}

static int (*s_prev_vprintf)(const char*, va_list) = NULL;

static int vprintf_tee(const char *fmt, va_list ap) {
    int r1 = 0;
    if (s_prev_vprintf) {
        r1 = s_prev_vprintf(fmt, ap);
    }

#if CONFIG_DEBUG_CONSOLE_GATT_TEE_LOGS
    if (s_log_mirror_enabled) {
        char buf[256];
        va_list ap2;
        va_copy(ap2, ap);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap2);
        va_end(ap2);
        
        if (n > 0) {
            size_t to_write = (n >= (int)sizeof(buf)) ? sizeof(buf)-1 : (size_t)n;
            ble_write((const uint8_t*)buf, to_write);
        }
    }
#endif

    return r1;
}

extern "C" void console_install_tee(void) {
#if CONFIG_DEBUG_CONSOLE_GATT_TEE_LOGS
    s_prev_vprintf = esp_log_set_vprintf(vprintf_tee);
    ESP_LOGI(TAG, "vprintf tee installed");
#else
    ESP_LOGI(TAG, "vprintf tee disabled (CONFIG_DEBUG_CONSOLE_GATT_TEE_LOGS=n)");
#endif
}
