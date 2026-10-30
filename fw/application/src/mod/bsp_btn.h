#ifndef BSP_BTN_H
#define BSP_BTN_H

#include <stdint.h>

typedef enum {
    BSP_BTN_EVENT_PRESSED,
    BSP_BTN_EVENT_RELEASED,
    BSP_BTN_EVENT_SHORT,
    BSP_BTN_EVENT_LONG,
    BSP_BTN_EVENT_REPEAT
} bsp_btn_event_t;


typedef void (* bsp_btn_event_cb_t)(uint8_t btn, bsp_btn_event_t evt);

void bsp_btn_init(bsp_btn_event_cb_t p_event_cb);

/**
 * @brief State of a physical button.
 *
 * @return 1 if the button is currently seen as pressed by the UI, 0 otherwise.
 */
uint8_t bsp_btn_is_down(uint8_t idx);


#endif