#ifndef BLE_CONTROL_PLANE_H
#define BLE_CONTROL_PLANE_H

#include <stdint.h>
#include "ble.h"
#include "ble_srv_common.h"

#define BLE_CONTROL_PLANE_SERVICE_UUID 0x0001
#define BLE_CONTROL_PLANE_BASE_UUID \
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x00,0x00,0x40,0x7e}

typedef enum {
    BLE_CP_MODE_IDLE = 0,
    BLE_CP_MODE_FILE_TRANSFER,
    BLE_CP_MODE_CONTROL
} ble_cp_mode_t;

void ble_control_plane_init(void);
ble_cp_mode_t ble_control_plane_mode_get(void);
void ble_control_plane_set_mode(ble_cp_mode_t mode);
uint8_t ble_control_plane_in_control_mode(void);
uint8_t ble_control_plane_uuid_type(void);

#endif