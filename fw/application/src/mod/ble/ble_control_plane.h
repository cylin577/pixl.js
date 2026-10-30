#ifndef BLE_CONTROL_PLANE_H
#define BLE_CONTROL_PLANE_H

#include <stdint.h>
#include "ble.h"
#include "ble_srv_common.h"

#define BLE_CONTROL_PLANE_SERVICE_UUID 0x0001
#define BLE_CONTROL_PLANE_BASE_UUID \
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x00,0x00,0x40,0x7e}

/* PixLink commands */
#define BLE_CP_CMD_ENTER_FILE_TRANSFER 0x01
#define BLE_CP_CMD_ENTER_CONTROL 0x02
#define BLE_CP_CMD_BUTTON_STATE 0x03

/* PixLink status codes */
#define BLE_CP_STATUS_OK 0x00
#define BLE_CP_STATUS_ERROR 0x01
#define BLE_CP_STATUS_INVALID_TRANSITION 0x02
#define BLE_CP_STATUS_UNKNOWN_CMD 0xFF

typedef enum {
    BLE_CP_MODE_IDLE = 0,
    BLE_CP_MODE_FILE_TRANSFER,
    BLE_CP_MODE_CONTROL
} ble_cp_mode_t;

void ble_control_plane_init(void);
void ble_control_plane_set_mode(ble_cp_mode_t mode);
uint8_t ble_control_plane_in_control_mode(void);
uint8_t ble_control_plane_uuid_type(void);

#endif