#include "console_gatt.h"
#include "esp_log.h"
#include "nimble/ble.h"
#include "host/ble_gatt.h"

static const char *TAG = "dbg_console";

static uint16_t g_rx_val_handle = 0;
static uint16_t g_tx_val_handle = 0;
static volatile bool s_console_registered = false;

/* UUIDs: SVC 18EE2EF5-263D-4559-959F-4F9C429F9D10
          RX  18EE2EF5-263D-4559-959F-4F9C429F9D11
          TX  18EE2EF5-263D-4559-959F-4F9C429F9D12 */
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0x10,0x9D,0x9F,0x42,0x9C,0x4F,0x9F,0x95,0x59,0x45,0x3D,0x26,0xF5,0x2E,0xEE,0x18);
static const ble_uuid128_t UUID_RX  = BLE_UUID128_INIT(0x11,0x9D,0x9F,0x42,0x9C,0x4F,0x9F,0x95,0x59,0x45,0x3D,0x26,0xF5,0x2E,0xEE,0x18);
static const ble_uuid128_t UUID_TX  = BLE_UUID128_INIT(0x12,0x9D,0x9F,0x42,0x9C,0x4F,0x9F,0x95,0x59,0x45,0x3D,0x26,0xF5,0x2E,0xEE,0x18);

/* Forward declarations for callbacks implemented in debug_console.c */
extern int debug_console_gatt_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg);
extern int debug_console_gatt_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                                         struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_chr_def kConsoleChrs[] = {
    { .uuid=&UUID_RX.u, .access_cb=debug_console_gatt_access_rx,
#ifdef CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED
      .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
#else
      .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
#endif
      .val_handle=&g_rx_val_handle },
    { .uuid=&UUID_TX.u, .access_cb=debug_console_gatt_access_tx,
      .flags=BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
      .val_handle=&g_tx_val_handle },
    { 0 }
};

static const struct ble_gatt_svc_def kConsoleSvc[] = {
    { .type=BLE_GATT_SVC_TYPE_PRIMARY, .uuid=&UUID_SVC.u, .characteristics=kConsoleChrs },
    { 0 }
};

const struct ble_gatt_svc_def * dbg_console_get_service_defs(void) {
    return kConsoleSvc;
}

void dbg_console_mark_registered(bool ok) {
    s_console_registered = ok && g_rx_val_handle && g_tx_val_handle;
    ESP_LOGI(TAG, "Console GATT registered=%d RX=%u TX=%u",
             s_console_registered, g_rx_val_handle, g_tx_val_handle);
    if (!s_console_registered) {
        ESP_LOGE(TAG, "Console registration invalid; advertising will be blocked.");
    }
}

int dbg_console_adv_guard(void) {
    if (!s_console_registered || !g_rx_val_handle || !g_tx_val_handle) {
        ESP_LOGE(TAG, "Console ADV blocked: not registered (reg=%d RX=%u TX=%u)",
                 (int)s_console_registered, g_rx_val_handle, g_tx_val_handle);
        return -1;
    }
    return 0;
}

uint16_t dbg_console_tx_handle(void){ return g_tx_val_handle; }
uint16_t dbg_console_rx_handle(void){ return g_rx_val_handle; }
