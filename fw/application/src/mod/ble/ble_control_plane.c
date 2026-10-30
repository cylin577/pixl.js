#include "ble_control_plane.h"
#include "app_scheduler.h"
#include "ble.h"
#include "nrf_sdh_ble.h"
#include "nrf_log.h"
#include "nrf_gpio.h"
#include "mui_input.h"
#include "bsp_btn.h"
#include "mui_core.h"
#include "cache.h"
#include "nrf_delay.h"
#include "mini_app_launcher.h"
#include "mini_app_registry.h"
#include <stdio.h>
#include <string.h>

#define CP_CHECK(call)                                                             \
    do {                                                                           \
        ret_code_t err_ = (call);                                                  \
        if (err_ != NRF_SUCCESS) {                                                 \
            char m_[64];                                                           \
            snprintf(m_, sizeof(m_), "CP ERR %u line %u", (unsigned)err_,          \
                     (unsigned)__LINE__);                                          \
            NRF_LOG_ERROR("control plane init failed: %u at line %u", err_,        \
                          (unsigned)__LINE__);                                     \
            mui_panic(mui(), m_);                                                  \
            nrf_delay_ms(3000);                                                    \
            return;                                                                \
        }                                                                          \
    } while (0)

#define CP_FLAG_HEADER_VALID 0x01 /**< The 4 byte packet header was present and valid. */
#define CP_FLAG_PAYLOAD 0x02     /**< The command payload was present and valid. */

#define CP_BTN_COUNT 3
#define CP_RESP_LEN 4    /**< Length of a response frame. */
#define CP_PENDING_MAX 4 /**< Number of responses that can wait for a free TX slot. */

/* Command event handed over to the main loop context. Must stay <= APP_SCHED_MAX_EVENT_SIZE (4). */
typedef struct {
    uint8_t cmd;   /**< Command id as received. */
    uint8_t flags; /**< CP_FLAG_* bits. */
    uint8_t btn;   /**< Button states, bit0 = A, bit1 = B, bit2 = C. */
} cp_event_t;

static volatile ble_cp_mode_t m_mode = BLE_CP_MODE_IDLE;
static uint16_t m_service_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_tx_char_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_rx_char_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_rx_cccd_handle = BLE_CONN_HANDLE_INVALID;

/* Written by the SoftDevice IRQ, read by the main loop. */
static volatile uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
static volatile uint8_t m_notify_enabled = 0;
static volatile uint32_t m_conn_epoch = 0;   /**< Bumped on every connect and disconnect, identifies the peer set. */
static volatile uint8_t m_flush_needed = 0;  /**< The response queue holds something to send. */
static volatile uint8_t m_flush_posted = 0;  /**< A flush event is already queued. */

static uint8_t m_base_uuid_type = BLE_UUID_TYPE_VENDOR_BEGIN;
static uint8_t m_cp_ready = 0;
static uint8_t m_btn_state[CP_BTN_COUNT] = {0}; /**< Virtual button states, 1 = pressed. */
static uint8_t m_btn_owned[CP_BTN_COUNT] = {0}; /**< 1 = the virtual press owns the UI down state. */

/* The response queue belongs to the main loop context, the SoftDevice IRQ must not touch it. */
static uint8_t m_pending[CP_PENDING_MAX][CP_RESP_LEN]; /**< Responses waiting for a free TX slot. */
static uint8_t m_pending_count = 0;
static uint32_t m_pending_epoch = 0; /**< Connection the queued responses belong to. */

static ble_uuid_t m_service_uuid = {BLE_CONTROL_PLANE_SERVICE_UUID, BLE_UUID_TYPE_VENDOR_BEGIN};

static void cp_flush_event_handler(void *p_event_data, uint16_t event_size);

static ret_code_t cp_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len) {
    ble_gatts_hvx_params_t params = {
        .handle = m_rx_char_handle,
        .p_data = data,
        .p_len = &len,
        .type = BLE_GATT_HVX_NOTIFICATION,
    };
    return sd_ble_gatts_hvx(conn_handle, &params);
}

/**
 * @brief Send the queued responses.
 *
 * Main loop context only, the response queue is owned by the main loop.
 *
 * @return 1 if the flush stopped because the SoftDevice TX queue is full, 0 if
 *         the queue was emptied or the responses were dropped.
 */
static uint8_t cp_pending_flush(void) {
    while (m_pending_count > 0) {
        // Read the handle before the epoch: the IRQ bumps the epoch before it
        // writes the new handle, so a connection change is always visible in
        // the epoch and the queued responses are dropped instead of being sent
        // to the new connection.
        uint16_t conn_handle = m_conn_handle;
        uint32_t epoch = m_conn_epoch;
        if (epoch != m_pending_epoch || epoch != m_conn_epoch) {
            // The connection was replaced, the queued responses belong to a
            // dead peer.
            NRF_LOG_WARNING("CP: drop %u stale resp", (unsigned)m_pending_count);
            m_pending_count = 0;
            m_flush_needed = 0;
            return 0;
        }
        if (conn_handle == BLE_CONN_HANDLE_INVALID) {
            // No connection, the queued responses have nowhere to go.
            m_pending_count = 0;
            m_flush_needed = 0;
            return 0;
        }
        uint16_t len = CP_RESP_LEN;
        ret_code_t err_code = cp_notify(conn_handle, m_pending[0], len);
        if (err_code == NRF_ERROR_RESOURCES) {
            // The TX queue is still full, the TX complete event posts the next
            // flush attempt. The flush must not re-post itself here, the
            // scheduler queue would never empty and the main loop would spin.
            m_flush_needed = 1;
            return 1;
        }
        if (err_code != NRF_SUCCESS) {
            NRF_LOG_ERROR("CP: notify err %u", (unsigned)err_code);
        }
        if (epoch != m_conn_epoch) {
            // The connection was replaced while the response was sent, the
            // queued responses belong to the old connection. The head was
            // already handed to the stack, so it is not part of the drop count.
            NRF_LOG_WARNING("CP: drop %u stale resp", (unsigned)(m_pending_count - 1));
            m_pending_count = 0;
            m_flush_needed = 0;
            return 0;
        }
        // The response was sent, or it can not be sent at all, drop it.
        m_pending_count--;
        for (uint8_t i = 0; i < m_pending_count; i++) {
            memcpy(m_pending[i], m_pending[i + 1], CP_RESP_LEN);
        }
    }
    m_flush_needed = 0;
    return 0;
}

/**
 * @brief Ask the main loop to send the queued responses.
 *
 * ISR safe, it only posts a scheduler event.
 */
static void cp_post_flush_event(void) {
    if (!m_flush_needed || m_flush_posted) {
        return;
    }
    m_flush_posted = 1;
    if (app_sched_event_put(NULL, 0, cp_flush_event_handler) != NRF_SUCCESS) {
        // The scheduler queue is full, the next TX complete event or the next
        // service call tries again.
        m_flush_posted = 0;
    }
}

/**
 * @brief Send a response.
 *
 * Main loop context only, the response queue is owned by the main loop.
 */
static void cp_respond(uint8_t cmd, uint8_t status) {
    // Read the handle before the epoch: the IRQ bumps the epoch before it
    // writes the new handle, so a connection change is always visible in the
    // epoch and the response is not sent to the new connection.
    uint16_t conn_handle = m_conn_handle;
    uint32_t epoch = m_conn_epoch;
    if (conn_handle == BLE_CONN_HANDLE_INVALID || !m_notify_enabled) {
        NRF_LOG_DEBUG("CP: resp dropped");
        return;
    }
    if (epoch != m_conn_epoch) {
        // The connection was replaced between the two reads, the response
        // belongs to the old peer, the new peer must not get it.
        NRF_LOG_WARNING("CP: resp dropped, conn changed");
        return;
    }

    uint8_t resp[CP_RESP_LEN] = {cmd, status, 0x00, 0x00};
    ret_code_t err_code = cp_notify(conn_handle, resp, (uint16_t)sizeof(resp));
    if (err_code == NRF_ERROR_RESOURCES) {
        // The notify queue is full, keep the response and send it as soon as a
        // notification transmission completes.
        if (epoch != m_conn_epoch) {
            // The connection was replaced while the response was created, drop it.
            NRF_LOG_WARNING("CP: resp dropped, conn changed");
            return;
        }
        if (m_pending_count < CP_PENDING_MAX) {
            if (m_pending_count > 0 && m_pending_epoch != epoch) {
                // The connection was replaced, the queued responses are stale.
                NRF_LOG_WARNING("CP: drop %u stale resp", (unsigned)m_pending_count);
                m_pending_count = 0;
            }
            if (m_pending_count == 0) {
                m_pending_epoch = epoch;
            }
            memcpy(m_pending[m_pending_count], resp, CP_RESP_LEN);
            m_pending_count++;
            m_flush_needed = 1;
            // Try it right away, the TX queue may have room already.
            cp_post_flush_event();
        } else {
            NRF_LOG_ERROR("CP: response queue full, response dropped");
        }
    } else if (err_code != NRF_SUCCESS) {
        NRF_LOG_ERROR("CP: notify err %u", (unsigned)err_code);
    }
}

/**
 * @brief Service the response queue.
 *
 * Main loop context only, the response queue is owned by the main loop.
 */
static void cp_service(void) {
    uint8_t wait_tx = 0;
    if (m_flush_needed) {
        wait_tx = cp_pending_flush();
    }
    if (wait_tx) {
        // The SoftDevice TX queue is full, the TX complete event posts the
        // next flush attempt. Re-posting the flush here would keep the
        // scheduler queue busy and block the main loop.
        return;
    }
    if (m_flush_needed) {
        // The flush did not finish and it is not waiting for a TX slot, so the
        // flush event was lost: re-arm it.
        cp_post_flush_event();
    }
}

static void cp_flush_event_handler(void *p_event_data, uint16_t event_size) {
    (void)p_event_data;
    (void)event_size;
    m_flush_posted = 0;
    cp_service();
}

static void set_virtual_button_state(uint8_t idx, uint8_t state) {
    if (state == m_btn_state[idx]) {
        return;
    }
    m_btn_state[idx] = state;
    if (state) {
        // A physical press that is still down already owns the UI state, so the
        // virtual press must not emit a second press event.
        if (bsp_btn_is_down(idx)) {
            m_btn_owned[idx] = 0;
            return;
        }
        m_btn_owned[idx] = 1;
        mui_input_on_bsp_btn_event(idx, BSP_BTN_EVENT_PRESSED);
    } else {
        if (m_btn_owned[idx]) {
            // Same order as a physical click: release first, then the short press.
            mui_input_on_bsp_btn_event(idx, BSP_BTN_EVENT_RELEASED);
            mui_input_on_bsp_btn_event(idx, BSP_BTN_EVENT_SHORT);
        }
        m_btn_owned[idx] = 0;
    }
}

static void apply_button_state(uint8_t btn) {
    for (uint8_t i = 0; i < CP_BTN_COUNT; i++) {
        set_virtual_button_state(i, (btn >> i) & 0x01);
    }
}

static void release_virtual_buttons(void) {
    for (uint8_t i = 0; i < CP_BTN_COUNT; i++) {
        if (!m_btn_owned[i]) {
            continue;
        }
        m_btn_owned[i] = 0;
        m_btn_state[i] = 0;
        // Release only, no short press.
        mui_input_on_bsp_btn_event(i, BSP_BTN_EVENT_RELEASED);
    }
}

static void handle_command(const cp_event_t *evt) {
    switch (evt->cmd) {
    case BLE_CP_CMD_ENTER_FILE_TRANSFER: {
        if (!(evt->flags & CP_FLAG_HEADER_VALID)) {
            cp_respond(evt->cmd, BLE_CP_STATUS_ERROR);
            return;
        }
        if (m_mode == BLE_CP_MODE_FILE_TRANSFER) {
            cp_respond(evt->cmd, BLE_CP_STATUS_INVALID_TRANSITION);
            return;
        }
        // The launcher kills the current app, which may reset the CP mode, so set the
        // mode after the launch.
        mini_app_launcher_run(mini_app_launcher(), MINI_APP_ID_BLE);
        ble_control_plane_set_mode(BLE_CP_MODE_FILE_TRANSFER);
        cp_respond(evt->cmd, BLE_CP_STATUS_OK);
    } break;

    case BLE_CP_CMD_ENTER_CONTROL: {
        if (!(evt->flags & CP_FLAG_HEADER_VALID)) {
            cp_respond(evt->cmd, BLE_CP_STATUS_ERROR);
            return;
        }
        if (m_mode == BLE_CP_MODE_CONTROL) {
            cp_respond(evt->cmd, BLE_CP_STATUS_INVALID_TRANSITION);
            return;
        }
        ble_control_plane_set_mode(BLE_CP_MODE_CONTROL);
        cp_respond(evt->cmd, BLE_CP_STATUS_OK);
    } break;

    case BLE_CP_CMD_BUTTON_STATE: {
        // Fire and forget: malformed or out of mode updates are silently ignored.
        if (!(evt->flags & CP_FLAG_HEADER_VALID) || !(evt->flags & CP_FLAG_PAYLOAD)) {
            return;
        }
        if (m_mode != BLE_CP_MODE_CONTROL) {
            return;
        }
        apply_button_state(evt->btn);
    } break;

    default:
        cp_respond(evt->cmd, BLE_CP_STATUS_UNKNOWN_CMD);
        break;
    }
}

static void cp_event_handler(void *p_event_data, uint16_t event_size) {
    // The queued responses belong to older commands, send them first.
    cp_service();
    handle_command((const cp_event_t *)p_event_data);
}

static void schedule_command(const uint8_t *data, uint16_t len) {
    cp_event_t evt = {0};
    evt.cmd = data[0];
    if (len >= 4 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x00) {
        evt.flags |= CP_FLAG_HEADER_VALID;
    }
    if (evt.cmd == BLE_CP_CMD_BUTTON_STATE && len == 7) {
        evt.flags |= CP_FLAG_PAYLOAD;
        evt.btn = (data[4] ? 0x01 : 0x00) | (data[5] ? 0x02 : 0x00) | (data[6] ? 0x04 : 0x00);
    }

    // The BLE observer runs in the SoftDevice IRQ, but the command handling needs the
    // main loop context (app launch, UI events), so hand it over to the scheduler.
    uint32_t err_code = app_sched_event_put(&evt, sizeof(evt), cp_event_handler);
    if (err_code != NRF_SUCCESS) {
        NRF_LOG_ERROR("CP: queue err %u", (unsigned)err_code);
        // The button state update never gets a response.
        if (evt.cmd != BLE_CP_CMD_BUTTON_STATE) {
            // The scheduler queue is full, so the error response is sent right
            // away. The response queue belongs to the main loop, so it must not
            // be touched from here, the notification is sent directly instead.
            uint16_t conn_handle = m_conn_handle;
            uint8_t resp[CP_RESP_LEN] = {evt.cmd, BLE_CP_STATUS_ERROR, 0x00, 0x00};
            if (conn_handle == BLE_CONN_HANDLE_INVALID || !m_notify_enabled) {
                NRF_LOG_ERROR("CP: error resp dropped, no connection");
            } else {
                ret_code_t notify_err = cp_notify(conn_handle, resp, (uint16_t)sizeof(resp));
                if (notify_err != NRF_SUCCESS) {
                    NRF_LOG_ERROR("CP: error resp dropped, notify err %u", (unsigned)notify_err);
                }
            }
        }
    }
}

static void on_ble_evt(ble_evt_t const *p_ble_evt, void *p_context) {
    switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED: {
        // Bump the epoch before the handle is written: a reader that takes the
        // handle first and then the epoch always sees the new epoch, so the
        // responses that were queued for the old connection are dropped.
        m_conn_epoch++;
        m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        m_notify_enabled = 0;
        // A bonded peer may already have notifications enabled, so read the CCCD
        // back instead of waiting for a write event.
        uint8_t cccd_value[2] = {0};
        ble_gatts_value_t gatts_val = {
            .len = (uint16_t)sizeof(cccd_value),
            .offset = 0,
            .p_value = cccd_value,
        };
        ret_code_t err_code = sd_ble_gatts_value_get(m_conn_handle, m_rx_cccd_handle, &gatts_val);
        if (err_code == NRF_SUCCESS) {
            m_notify_enabled = ble_srv_is_notification_enabled(cccd_value) ? 1 : 0;
        }
    } break;

    case BLE_GAP_EVT_DISCONNECTED:
        // Bump the epoch before the handle is invalidated, the same order as
        // on connect: the epoch tracks "the peer set changed", not only "a
        // connect happened", so the responses of the old peer are stale.
        m_conn_epoch++;
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        m_notify_enabled = 0;
        break;

    case BLE_GATTS_EVT_HVN_TX_COMPLETE:
        // A notification was transmitted, so the TX queue may have room again.
        // The response queue belongs to the main loop, so only ask it to flush.
        cp_post_flush_event();
        break;

    case BLE_GATTS_EVT_WRITE: {
        const ble_gatts_evt_write_t *w = &p_ble_evt->evt.gatts_evt.params.write;
        if (w->handle == m_rx_cccd_handle && w->len == 2) {
            m_notify_enabled = ble_srv_is_notification_enabled(w->data) ? 1 : 0;
        } else if (w->handle == m_tx_char_handle && w->len > 0) {
            schedule_command(w->data, w->len);
        }
    } break;

    default:
        break;
    }
}

void ble_control_plane_set_mode(ble_cp_mode_t mode) {
    if (m_mode == BLE_CP_MODE_CONTROL && mode != BLE_CP_MODE_CONTROL) {
        // Leaving control mode: let the UI know that the shadowed buttons are gone.
        release_virtual_buttons();
    }
    if (mode == BLE_CP_MODE_CONTROL && m_mode != BLE_CP_MODE_CONTROL) {
        // Start with a clean virtual state, the physical buttons keep their own
        // state, so a held button is not pressed twice.
        for (uint8_t i = 0; i < CP_BTN_COUNT; i++) {
            m_btn_state[i] = 0;
            m_btn_owned[i] = 0;
        }
    }
    m_mode = mode;
}

uint8_t ble_control_plane_in_control_mode(void) { return m_mode == BLE_CP_MODE_CONTROL; }
uint8_t ble_control_plane_uuid_type(void) { return m_cp_ready ? m_base_uuid_type : BLE_UUID_TYPE_UNKNOWN; }

void ble_control_plane_init(void) {
    ret_code_t err_code;
    ble_uuid128_t base_uuid = BLE_CONTROL_PLANE_BASE_UUID;
    err_code = sd_ble_uuid_vs_add(&base_uuid, &m_base_uuid_type);
    CP_CHECK(err_code);
    m_service_uuid.type = m_base_uuid_type;

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &m_service_uuid, &m_service_handle);
    CP_CHECK(err_code);

    ble_gatts_char_md_t char_md = {0};
    ble_gatts_attr_md_t attr_md = {0};
    ble_gatts_attr_t attr_char_value = {0};
    ble_uuid_t char_uuid = {0x0002, m_base_uuid_type};
    ble_gatts_char_handles_t handles = {0};
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    char_md.char_props.write = 1;
    char_md.char_props.write_wo_resp = 1;
    attr_char_value.p_uuid = &char_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.max_len = 20;
    attr_char_value.init_len = 0;
    err_code = sd_ble_gatts_characteristic_add(m_service_handle, &char_md, &attr_char_value, &handles);
    CP_CHECK(err_code);
    m_tx_char_handle = handles.value_handle;

    memset(&char_md, 0, sizeof(char_md));
    memset(&attr_md, 0, sizeof(attr_md));
    memset(&attr_char_value, 0, sizeof(attr_char_value));
    ble_uuid_t rx_uuid = {0x0003, m_base_uuid_type};
    ble_gatts_attr_md_t cccd_md = {0};
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    cccd_md.vloc = BLE_GATTS_VLOC_STACK;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    char_md.char_props.notify = 1;
    char_md.p_cccd_md = &cccd_md;
    attr_char_value.p_uuid = &rx_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.max_len = 20;
    attr_char_value.init_len = 0;
    err_code = sd_ble_gatts_characteristic_add(m_service_handle, &char_md, &attr_char_value, &handles);
    CP_CHECK(err_code);
    m_rx_char_handle = handles.value_handle;
    m_rx_cccd_handle = handles.cccd_handle;

    m_cp_ready = 1;
    NRF_SDH_BLE_OBSERVER(m_cp_observer, 3, on_ble_evt, NULL);
    NRF_LOG_INFO("PixLink init");
}
