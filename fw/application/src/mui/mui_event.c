#include "mui_event.h"
#include "nrf_log.h"

void mui_event_queue_init(mui_event_queue_t *p_queue) {
    p_queue->redraw_pending = false;
    mui_event_deque_init(p_queue->event_deque);
}
void mui_event_set_callback(mui_event_queue_t *p_queue, mui_event_handler_t dispatcher,
                            void *context) {
    p_queue->dispatch_context = context;
    p_queue->dispatcher = dispatcher;
}

void mui_event_post(mui_event_queue_t *p_queue, mui_event_t *p_event) {
    // CRTIAL_ENTER
    if (mui_event_deque_size(p_queue->event_deque) >= MAX_EVENT_MSG) {
        NRF_LOG_WARNING("event buffer is FULL!!");
        return;
    }
    // coalesce pending REDRAW events: one queued redraw is always enough
    if (p_event->id == MUI_EVENT_ID_REDRAW) {
        if (p_queue->redraw_pending) {
            return;
        }
        p_queue->redraw_pending = true;
    }
    mui_event_t *p_new = mui_event_deque_push_back_new(p_queue->event_deque);
    memcpy(p_new, p_event, sizeof(mui_event_t));
}

void mui_event_dispatch(mui_event_queue_t *p_queue) {
    mui_event_t event;
    while (!mui_event_deque_empty_p(p_queue->event_deque)) {
        mui_event_deque_pop_front(&event, p_queue->event_deque);
        if (event.id == MUI_EVENT_ID_REDRAW) {
            p_queue->redraw_pending = false;
        }
        p_queue->dispatcher(p_queue->dispatch_context, &event);
    }
}

void mui_event_dispatch_now(mui_event_queue_t *p_queue, mui_event_t* p_event){
    if (p_event->id == MUI_EVENT_ID_REDRAW) {
        p_queue->redraw_pending = false;
    }
    p_queue->dispatcher(p_queue->dispatch_context, p_event);
}
