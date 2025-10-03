#if CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_efuse.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "debug_console.h"

static const char *TAG = "dbg_console";

/* Nordic UART Service UUIDs */
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_RX  = BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_TX  = BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* GATT handles */
static uint16_t g_tx_val_handle = 0;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool     g_notify_enabled = false;
static bool     g_encrypted = false;   /* link encryption state */
static bool     g_bonded    = false;   /* peer bonding state */

/* Defaults come from Kconfig (both ON by default) */
static bool     s_require_bond = CONFIG_BLE_CONSOLE_REQUIRE_BOND;
static bool     s_rate_limit   = CONFIG_BLE_CONSOLE_RATE_LIMIT;

/* Simple token bucket rate limiter (notifications per 100ms window) */
#define DC_RATE_WINDOW_US   (100000)   /* 100ms */
#define DC_RATE_TOKENS_MAX  (20)       /* up to ~200 notif/s burst-ish */
static int      s_tokens = DC_RATE_TOKENS_MAX;
static int64_t  s_window_start_us = 0;

static inline bool dc_can_send_now(void)
{
    if (!s_rate_limit) return true;
    int64_t now = esp_timer_get_time();
    if (now - s_window_start_us > DC_RATE_WINDOW_US) {
        s_window_start_us = now;
        s_tokens = DC_RATE_TOKENS_MAX;
    }
    if (s_tokens <= 0) return false;
    s_tokens--;
    return true;
}

/* Forward declarations */
static int gatt_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event(struct ble_gap_event *ev, void *arg);
static void ensure_host_ready(void);

/* GATT service definition */
static const struct ble_gatt_svc_def g_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &UUID_RX.u,
                .access_cb = gatt_access_rx,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &UUID_TX.u,
                .access_cb = gatt_access_tx,
                .val_handle = &g_tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
            },
            {0}
        },
    },
    {0}
};

static int gatt_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    /* Enforce encryption (and, by default, bonding) on RX writes */
    if (!g_encrypted) {
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    if (s_require_bond && !g_bonded) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    /* Read flat data from mbuf and forward to bridge */
    uint16_t total = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t  buf[128];
    uint16_t copied = 0;

    while (copied < total) {
        uint16_t chunk = total - copied;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        int rc = os_mbuf_copydata(ctxt->om, copied, chunk, buf);
        if (rc != 0) break;
        console_bridge_feed_rx(buf, chunk);
        copied += chunk;
    }
    return 0;
}

static int gatt_access_tx(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    return 0;
}

void debug_console_notify(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return;
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    if (!g_notify_enabled) return;
    if (!g_encrypted) return;
    if (s_require_bond && !g_bonded) return;

    /* Determine per-conn ATT MTU; payload = MTU - 3 */
    uint16_t mtu = ble_att_mtu(g_conn_handle);
    uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;

    /* Chunk into notifications */
    size_t off = 0;
    while (off < len) {
        uint16_t n = (uint16_t)(len - off);
        if (n > max_payload) n = max_payload;

        struct os_mbuf *om = ble_hs_mbuf_from_flat(&data[off], n);
        if (!om) break;

        /* Obey rate limiter */
        if (!dc_can_send_now()) {
            os_mbuf_free_chain(om);
            break;
        }
        int rc = ble_gatts_notify(g_conn_handle, g_tx_val_handle, om);
        if (rc != 0) {
            os_mbuf_free_chain(om);
            break;
        }
        off += n;
        taskYIELD();
    }
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            g_conn_handle = ev->connect.conn_handle;
            ESP_LOGI(TAG, "Connected (handle=%d)", g_conn_handle);
            /* Query current security state and cache encryption flag */
            struct ble_gap_conn_desc d;
            if (ble_gap_conn_find(g_conn_handle, &d) == 0) {
                g_encrypted = d.sec_state.encrypted;
                g_bonded    = d.sec_state.bonded;
            } else {
                g_encrypted = false;
                g_bonded    = false;
            }
        } else {
            ESP_LOGW(TAG, "Connect failed; restarting ADV (status=%d)", ev->connect.status);
            debug_console_start_adv();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", ev->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_notify_enabled = false;
        g_encrypted = false;
        g_bonded    = false;
        debug_console_start_adv();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        g_notify_enabled = ev->subscribe.cur_notify && g_encrypted && (!s_require_bond || g_bonded);
        ESP_LOGI(TAG, "Notify %s (enc=%d, bond=%d, require_bond=%d)",
                 g_notify_enabled ? "ENABLED" : "DISABLED",
                 (int)g_encrypted, (int)g_bonded, (int)s_require_bond);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        g_encrypted = (ev->enc_change.status == 0) && ev->enc_change.encrypted;
        /* Refresh full conn desc to read 'bonded' flag too */
        { struct ble_gap_conn_desc d;
          if (ble_gap_conn_find(g_conn_handle, &d) == 0) g_bonded = d.sec_state.bonded; }
        ESP_LOGI(TAG, "Security: enc=%d bond=%d (require_bond=%d)", (int)g_encrypted, (int)g_bonded, (int)s_require_bond);
        if (!g_encrypted) {
            g_notify_enabled = false;
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update: %d", ev->mtu.value);
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
            ESP_LOGI(TAG, "=== Passkey: %06lu ===", (unsigned long)ev->passkey.params.numcmp);
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = ev->passkey.params.numcmp};
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void ensure_host_ready(void)
{
    while (!ble_hs_synced()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t debug_console_init(void)
{
    ensure_host_ready();
    
    /* Configure Security Manager for encryption + bonding + MITM */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    
    int rc = 0;
    rc = ble_gatts_count_cfg(g_svcs);    
    ESP_RETURN_ON_FALSE(rc==0, ESP_FAIL, TAG, "count_cfg=%d", rc);
    rc = ble_gatts_add_svcs(g_svcs);     
    ESP_RETURN_ON_FALSE(rc==0, ESP_FAIL, TAG, "add_svcs=%d", rc);
    ESP_LOGI(TAG, "GATT service registered (tx_handle=%u)", g_tx_val_handle);

    /* Bring up the bridge (line assembler + vprintf mirror) */
    ESP_RETURN_ON_ERROR(console_bridge_init(), TAG, "bridge init failed");
    /* Start with logs mirroring OFF; user can enable via 'logs on' */
    console_bridge_set_log_mirror(false);
    return ESP_OK;
}

esp_err_t debug_console_start_adv(void)
{
    struct ble_gap_adv_params ap = {0};
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;

    /* Get MAC address for device name suffix */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char name[17];
    snprintf(name, sizeof(name), "LIGHT-DBG-%02X%02X", mac[4], mac[5]);

    uint8_t adv[31]; 
    uint8_t len = 0;
    /* Flags */
    adv[len++] = 2; 
    adv[len++] = BLE_HS_ADV_TYPE_FLAGS; 
    adv[len++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    /* Name (complete) */
    uint8_t nlen = (uint8_t)strlen(name);
    adv[len++] = nlen + 1; 
    adv[len++] = BLE_HS_ADV_TYPE_COMP_NAME; 
    memcpy(&adv[len], name, nlen); 
    len += nlen;
    /* 128-bit UUID (complete list) */
    uint8_t ulen = 16;
    adv[len++] = ulen + 1; 
    adv[len++] = BLE_HS_ADV_TYPE_COMP_UUIDS128;
    memcpy(&adv[len], UUID_SVC.value, ulen); 
    len += ulen;

    ble_gap_adv_set_data(adv, len);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event, NULL);
    if (rc) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Advertising (console) as %s", name);
    return ESP_OK;
}

esp_err_t debug_console_stop_adv(void)
{
    /* Disconnect if connected */
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    
    int rc = ble_gap_adv_stop();
    if (rc && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "adv_stop rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Advertising stopped (console)");
    return ESP_OK;
}

bool debug_console_is_connected(void)
{
    return g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void debug_console_set_require_bond(bool enable) { s_require_bond = enable; }
bool debug_console_get_require_bond(void)        { return s_require_bond; }
void debug_console_set_rate_limit(bool enable)   { s_rate_limit   = enable; }
bool debug_console_get_rate_limit(void)          { return s_rate_limit; }

#endif /* CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED */
