#include "sdkconfig.h"

#if CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "debug_console.h"
#include "console_gatt.h"

static const char *TAG = "dbg_console";

static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(0x10,0x9D,0x9F,0x42,0x9C,0x4F,0x9F,0x95,0x59,0x45,0x3D,0x26,0xF5,0x2E,0xEE,0x18);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool     g_notify_enabled = false;
static bool     g_ind_subscribed = false;
static bool     g_encrypted = false;
static bool     g_bonded    = false;

/* Defaults come from Kconfig (both ON by default) */
#ifdef CONFIG_BLE_CONSOLE_REQUIRE_BOND
static bool     s_require_bond = true;
#else
static bool     s_require_bond = false;
#endif

#ifdef CONFIG_BLE_CONSOLE_RATE_LIMIT
static bool     s_rate_limit = true;
#else
static bool     s_rate_limit = false;
#endif

static bool     s_dbg_inited = false;

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

static int gap_event(struct ble_gap_event *ev, void *arg);
static void ensure_host_ready(void);

static void dump_hex(const uint8_t *p, int len)
{
    if (!p || len <= 0) return;
    char line[80];
    int o = 0;
    for (int i = 0; i < len; i++) {
        o += snprintf(line + o, sizeof(line) - o, "%02X ", p[i]);
        if ((i % 16) == 15 || i == len - 1) {
            ESP_LOGV(TAG, "HEX: %s", line);
            o = 0;
        }
    }
}

int debug_console_gatt_access_rx(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }
    
    /* Read flat data from mbuf */
    uint16_t total = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[244];
    int len = total;
    if (len > sizeof(buf)) len = sizeof(buf);
    
    int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
    if (rc != 0) {
        ESP_LOGE(TAG, "RX: mbuf_copydata failed rc=%d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    
    ESP_LOGI(TAG, "RX WRITE on handle=%u len=%d (expect RX=%u)", attr_handle, len, dbg_console_rx_handle());
    dump_hex(buf, len);
    
    /* ASCII representation (best effort) */
    char asc[245];
    int alen = len < 244 ? len : 244;
    memcpy(asc, buf, alen);
    asc[alen] = 0;
    ESP_LOGI(TAG, "ASCII: \"%s\"", asc);
    
    /* Enforce encryption (and, by default, bonding) on RX writes */
    if (!g_encrypted) {
        ESP_LOGW(TAG, "RX write rejected: not encrypted");
        return BLE_ATT_ERR_INSUFFICIENT_ENC;
    }
    if (s_require_bond && !g_bonded) {
        ESP_LOGW(TAG, "RX write rejected: not bonded (require_bond=%d)", (int)s_require_bond);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    
    if (g_ind_subscribed) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        if (om) {
            int echo_rc = ble_gatts_indicate_custom(conn_handle, dbg_console_tx_handle(), om);
            if (echo_rc != 0) {
                os_mbuf_free_chain(om);
            }
            ESP_LOGI(TAG, "echo indicate rc=%d", echo_rc);
        } else {
            ESP_LOGE(TAG, "echo: mbuf alloc failed");
        }
    } else {
        ESP_LOGW(TAG, "no subscriber; skipping echo");
    }
    
    /* Forward to console bridge for line assembly + command processing */
    uint16_t copied = 0;
    while (copied < total) {
        uint16_t chunk = total - copied;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        rc = os_mbuf_copydata(ctxt->om, copied, chunk, buf);
        if (rc != 0) break;
        console_bridge_feed_rx(buf, chunk);
        copied += chunk;
    }
    
    return 0;
}

int debug_console_gatt_access_tx(uint16_t conn_handle, uint16_t attr_handle,
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
        int rc = ble_gatts_notify_custom(g_conn_handle, dbg_console_tx_handle(), om);
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
            ESP_LOGI(TAG, "CONNECT status=%d handle=%d", ev->connect.status, g_conn_handle);
            /* Query current security state and cache encryption flag */
            struct ble_gap_conn_desc d;
            if (ble_gap_conn_find(g_conn_handle, &d) == 0) {
                g_encrypted = d.sec_state.encrypted;
                g_bonded    = d.sec_state.bonded;
                ESP_LOGI(TAG, "Connection security: enc=%d bond=%d", (int)g_encrypted, (int)g_bonded);
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
        ESP_LOGI(TAG, "DISCONNECT reason=0x%02X", ev->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_notify_enabled = false;
        g_ind_subscribed = false;
        g_encrypted = false;
        g_bonded    = false;
        debug_console_start_adv();
        break;
        
    case BLE_GAP_EVENT_SUBSCRIBE:
        g_ind_subscribed = ev->subscribe.cur_indicate || ev->subscribe.cur_notify;
        g_notify_enabled = g_ind_subscribed && g_encrypted && (!s_require_bond || g_bonded);
        
        ESP_LOGI(TAG, "SUBSCRIBE: attr=%u -> ind=%d", ev->subscribe.attr_handle, g_ind_subscribed);
        
        /* Send test indication to prove TX path works */
        if (g_ind_subscribed) {
            static const uint8_t pong[] = "pong\r\n";
            struct os_mbuf *om = ble_hs_mbuf_from_flat(pong, sizeof(pong) - 1);
            if (om) {
                int rc = ble_gatts_indicate_custom(g_conn_handle, dbg_console_tx_handle(), om);
                if (rc != 0) {
                    os_mbuf_free_chain(om);
                }
                ESP_LOGI(TAG, "test indicate rc=%d (tx_handle=%u)", rc, dbg_console_tx_handle());
            } else {
                ESP_LOGE(TAG, "test indicate: mbuf alloc failed");
            }
        }
        break;
        
    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Check encryption status from connection descriptor */
        { struct ble_gap_conn_desc d;
          if (ble_gap_conn_find(g_conn_handle, &d) == 0) {
              g_encrypted = d.sec_state.encrypted;
              g_bonded = d.sec_state.bonded;
          } else {
              g_encrypted = false;
              g_bonded = false;
          }
        }
        ESP_LOGI(TAG, "Security: enc=%d bond=%d (require_bond=%d)", (int)g_encrypted, (int)g_bonded, (int)s_require_bond);
        if (!g_encrypted) {
            g_notify_enabled = false;
            g_ind_subscribed = false;
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
    if (s_dbg_inited) {
        ESP_LOGW(TAG, "BLE console already initialized");
        return ESP_OK;
    }
    
    ensure_host_ready();
    
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    
    ESP_RETURN_ON_ERROR(console_bridge_init(), TAG, "bridge init failed");
    console_bridge_set_log_mirror(false);
    
    s_dbg_inited = true;
    return ESP_OK;
}

esp_err_t debug_console_start_adv(void)
{
    ensure_host_ready();
    
    if (dbg_console_adv_guard() != 0) {
        return ESP_FAIL;
    }
    
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char name[17];
    snprintf(name, sizeof(name), "LIGHT-DBG-%02X%02X", mac[4], mac[5]);

    /* 1) Use GENERAL discoverable + no-BR/EDR flags in ADV data */
    uint8_t adv[31], adv_len = 0;
    adv[adv_len++] = 2; 
    adv[adv_len++] = BLE_HS_ADV_TYPE_FLAGS;
    adv[adv_len++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    /* 2) Include 128-bit Service UUID in ADV (not only SR) */
    adv[adv_len++] = 17; 
    adv[adv_len++] = BLE_HS_ADV_TYPE_COMP_UUIDS128;
    memcpy(&adv[adv_len], UUID_SVC.value, 16); 
    adv_len += 16;
    
    ble_gap_adv_set_data(adv, adv_len);

    /* 3) Put the device name in Scan Response */
    uint8_t sr[31], sr_len = 0;
    uint8_t nlen = (uint8_t)strlen(name);
    sr[sr_len++] = nlen + 1;
    sr[sr_len++] = BLE_HS_ADV_TYPE_COMP_NAME;
    memcpy(&sr[sr_len], name, nlen); 
    sr_len += nlen;
    
    ble_gap_adv_rsp_set_data(sr, sr_len);

    /* 4) Sensible params, connectable + general discovery */
    struct ble_gap_adv_params ap = {0};
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ap.itvl_min = 0x00A0;  /* 100 ms interval */
    ap.itvl_max = 0x00A0;  /* 100 ms interval */
    ap.channel_map = 0x07; /* All channels */

    /* 5) Try RANDOM first (falls back to PUBLIC if needed) */
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &ap, gap_event, NULL);
    if (rc) {
        ESP_LOGW(TAG, "adv_start RANDOM rc=%d, retrying PUBLIC", rc);
        rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &ap, gap_event, NULL);
    }
    ESP_LOGI(TAG, "adv_start rc=%d", rc);
    
    if (rc) {
        ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
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

bool debug_console_is_initialized(void)
{
    return s_dbg_inited;
}

void debug_console_set_require_bond(bool enable) { s_require_bond = enable; }
bool debug_console_get_require_bond(void)        { return s_require_bond; }
void debug_console_set_rate_limit(bool enable)   { s_rate_limit   = enable; }
bool debug_console_get_rate_limit(void)          { return s_rate_limit; }

#endif /* CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED */
