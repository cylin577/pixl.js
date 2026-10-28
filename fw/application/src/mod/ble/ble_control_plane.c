#include "ble_control_plane.h"
#include "app_scheduler.h"
#include "ble.h"
#include "nrf_sdh_ble.h"
#include "nrf_log.h"
#include "nrf_gpio.h"
#include <string.h>

static ble_cp_mode_t m_mode = BLE_CP_MODE_IDLE;
static uint16_t m_service_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_tx_char_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_rx_char_handle = BLE_CONN_HANDLE_INVALID;
static uint8_t m_base_uuid_type = BLE_UUID_TYPE_VENDOR_BEGIN;

static ble_uuid_t m_service_uuid = {BLE_CONTROL_PLANE_SERVICE_UUID, BLE_UUID_TYPE_VENDOR_BEGIN};

static void apply_button_state(uint8_t a, uint8_t b, uint8_t c) {
    (void)a; (void)b; (void)c;
    /* software debounce / apply handled by bsp; placeholder */
}

static void handle_write(const uint8_t *data, uint16_t len) {
    if (len < 4) return;
    uint8_t cmd = data[0];
    /* status = data[1]; chunk = data[2]|data[3]<<8 */
    if (cmd == 0x01) {
        /* enter file transfer mode */
        ble_control_plane_set_mode(BLE_CP_MODE_FILE_TRANSFER);
        /* defer app launch via scheduler */
    } else if (cmd == 0x02) {
        ble_control_plane_set_mode(BLE_CP_MODE_CONTROL);
    } else if (cmd == 0x03) {
        if (m_mode != BLE_CP_MODE_CONTROL) return; /* ignore */
        if (len < 7) return;
        apply_button_state(data[4], data[5], data[6]);
    } else {
        /* unknown */
    }
}

static void on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context) {
    if (p_ble_evt->header.evt_id == BLE_GATTS_EVT_WRITE) {
        const ble_gatts_evt_write_t *w = &p_ble_evt->evt.gatts_evt.params.write;
        if (w->handle == m_tx_char_handle && w->len > 0) {
            handle_write(w->data, w->len);
        }
    }
}

ble_cp_mode_t ble_control_plane_mode_get(void) { return m_mode; }
void ble_control_plane_set_mode(ble_cp_mode_t mode) { m_mode = mode; }
uint8_t ble_control_plane_in_control_mode(void) { return m_mode == BLE_CP_MODE_CONTROL; }

void ble_control_plane_init(void) {
    ret_code_t err_code;
    ble_uuid128_t base_uuid = {0x9e,0xca,0xdc,0x24,0x0e,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00};
    err_code = sd_ble_uuid_vs_add(&base_uuid, &m_base_uuid_type);
    APP_ERROR_CHECK(err_code);
    m_service_uuid.type = m_base_uuid_type;

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &m_service_uuid, &m_service_handle);
    APP_ERROR_CHECK(err_code);

    /* TX char write */
    ble_gatts_char_md_t char_md = {0};
    ble_gatts_attr_md_t attr_md = {0};
    ble_gatts_attr_t attr_char_value = {0};
    ble_uuid_t char_uuid = {0x0002, m_base_uuid_type};
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    char_md.char_props.write = 1;
    char_md.char_props.write_wo_resp = 1;
    attr_char_value.p_uuid = &char_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.max_len = 20;
    attr_char_value.init_len = 0;
    err_code = sd_ble_gatts_characteristic_add(m_service_handle, &char_md, &attr_char_value, NULL);
    APP_ERROR_CHECK(err_code);

    NRF_SDH_BLE_OBSERVER(m_cp_observer, 3, on_ble_evt, NULL);
    NRF_LOG_INFO("BLE control plane init");
}