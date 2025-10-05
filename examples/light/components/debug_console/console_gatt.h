#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

const struct ble_gatt_svc_def * dbg_console_get_service_defs(void);

void dbg_console_mark_registered(bool ok);

int  dbg_console_adv_guard(void);

uint16_t dbg_console_tx_handle(void);
uint16_t dbg_console_rx_handle(void);

#ifdef __cplusplus
}
#endif
