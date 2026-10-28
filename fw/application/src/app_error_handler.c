#include "app_error.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "app_util_platform.h"
#include "nrf_strerror.h"

#if defined(SOFTDEVICE_PRESENT) && SOFTDEVICE_PRESENT
#include "nrf_sdm.h"
#endif

#include "mui_core.h"
#include "nrf_delay.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>


static void fault_show(const char *msg)
{
    mui_panic(mui(), (char *)msg);
    cache_clean();
    nrf_delay_ms(5000);
}

/*lint -save -e14 */
/**
 * Function is implemented as weak so that it can be overwritten by custom application error handler
 * when needed.
 */
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    __disable_irq();
    NRF_LOG_FINAL_FLUSH();

    char msg[96];

    if (id == NRF_FAULT_ID_SDK_ERROR)
    {
        error_info_t *p_info = (error_info_t *)info;
        const char *file = strrchr(p_info->p_file_name, '/');
        NRF_LOG_ERROR("ERROR %u [%s] at %s:%u PC 0x%08x", p_info->err_code,
                      nrf_strerror_get(p_info->err_code), p_info->p_file_name,
                      p_info->line_num, pc);
        snprintf(msg, sizeof(msg), "ERR %u %s:%u", (unsigned)p_info->err_code,
                 file ? file + 1 : p_info->p_file_name, (unsigned)p_info->line_num);
        fault_show(msg);
    }
    else if (id == NRF_FAULT_ID_SDK_ASSERT)
    {
        assert_info_t *p_info = (assert_info_t *)info;
        const char *file = strrchr(p_info->p_file_name, '/');
        NRF_LOG_ERROR("ASSERT at %s:%u", p_info->p_file_name, p_info->line_num);
        snprintf(msg, sizeof(msg), "ASSERT %s:%u", file ? file + 1 : p_info->p_file_name,
                 (unsigned)p_info->line_num);
        fault_show(msg);
    }
    else
    {
        NRF_LOG_ERROR("FAULT %u pc 0x%08x", (unsigned)id, (unsigned)pc);
    }

#if defined(DEBUG)
    app_error_save_and_stop(id, pc, info);
#else
    NVIC_SystemReset();
#endif
}



void HardFault_Handler(void)
{
    static volatile uint8_t shown = 0;

    uint32_t *sp = (uint32_t *) __get_MSP(); // Get stack pointer
    uint32_t ia = sp[12]; // Get instruction address from stack

    printf("Hard Fault at address: 0x%08x\r\n", (unsigned int)ia);

    cache_clean();

#ifndef DEBUG
    if (!shown)
    {
        char msg[48];
        shown = 1;
        snprintf(msg, sizeof(msg), "HARDFAULT %08x", (unsigned)ia);
        fault_show(msg);
    }
    NRF_LOG_WARNING("System reset");
    NVIC_SystemReset();
#else
    // endless loop here to wait debugger attach
    NRF_BREAKPOINT_COND;
    while(1)
        ;
#endif

}

void _exit(int status){
    NRF_LOG_WARNING("System reset: %d", status);
#ifndef DEBUG
    NVIC_SystemReset();
#else
    // endless loop here to wait debugger attach
    NRF_BREAKPOINT_COND;
    while(1)
        ;
#endif
}