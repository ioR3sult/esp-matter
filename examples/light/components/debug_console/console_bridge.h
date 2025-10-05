#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t console_bridge_init(void);
void console_bridge_set_log_mirror(bool enable);
void ble_console_on_subscribed(void);
void console_bridge_feed_rx(const uint8_t *data, size_t len);
void console_install_tee(void);

#ifdef __cplusplus
}
#endif
