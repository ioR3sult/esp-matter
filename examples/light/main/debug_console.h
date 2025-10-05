#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED

/* Existing public API */
esp_err_t debug_console_init(void);
esp_err_t debug_console_start_adv(void);
esp_err_t debug_console_stop_adv(void);
bool      debug_console_is_connected(void);
bool      debug_console_is_initialized(void);

int debug_console_gatt_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
int debug_console_gatt_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);

/* --- Bridge glue (console_bridge.c <-> debug_console.c) --- */

/* Called by bridge to send bytes out over BLE (will chunk to MTU). */
void debug_console_notify(const uint8_t *data, size_t len);

/* Called by GATT RX path to push incoming bytes into the bridge. */
void console_bridge_feed_rx(const uint8_t *data, size_t len);

/* Initializes bridge tasks/queues and installs vprintf hook. */
esp_err_t console_bridge_init(void);

/* Control whether vprintf-mirrored logs are sent over BLE */
void console_bridge_set_log_mirror(bool enable);
bool console_bridge_get_log_mirror(void);

/* Runtime toggles for bond requirement and TX rate limiting */
void debug_console_set_require_bond(bool enable);
bool debug_console_get_require_bond(void);
void debug_console_set_rate_limit(bool enable);
bool debug_console_get_rate_limit(void);

#else

static inline esp_err_t debug_console_init(void) { return ESP_OK; }
static inline esp_err_t debug_console_start_adv(void) { return ESP_OK; }
static inline esp_err_t debug_console_stop_adv(void) { return ESP_OK; }
static inline bool      debug_console_is_connected(void) { return false; }
static inline bool      debug_console_is_initialized(void) { return false; }

#endif

#ifdef __cplusplus
}
#endif
