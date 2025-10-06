#include "esp_console.h"
#include "esp_log.h"
#include "console_state.h"
#include "sdkconfig.h"
#include <stdio.h>

static int cmd_console_selftest(int argc, char **argv) {
    console_state_t *st = console_state();
    printf("console_selftest:\r\n");
    printf("  engine           : esp_console\r\n");
    printf("  conn_handle      : %u\r\n", (unsigned) st->conn_handle);
    printf("  tx_val_handle    : %u\r\n", (unsigned) st->tx_val_handle);
    printf("  notify_enabled   : %u\r\n", (unsigned) st->notify_enabled);
    printf("  CONFIG input     : %u\r\n", (unsigned) CONFIG_DEBUG_CONSOLE_GATT_INPUT);
    printf("  CONFIG tee_logs  : %u\r\n", (unsigned) CONFIG_DEBUG_CONSOLE_GATT_TEE_LOGS);
    printf("  CONFIG tx_notify : %u\r\n", (unsigned) 1);
#if CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED
    printf("  CONFIG encrypted : 1\r\n");
#else
    printf("  CONFIG encrypted : 0\r\n");
#endif
    return 0;
}

void register_console_selftest(void) {
    const esp_console_cmd_t cmd = {
        .command = "console_selftest",
        .help    = "Print BLE console wiring state and config bits",
        .hint    = NULL,
        .func    = &cmd_console_selftest,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
