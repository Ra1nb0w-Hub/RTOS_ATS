#include <stddef.h>
#include <string.h>

#include "ats_sys.h"
#include "ats_rpc.h"
#include "ats_error.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"

__attribute__((noreturn)) void FaultHandler(uint32_t *sp)
{
    uint32_t pc = sp[6];
    uint32_t lr = sp[5];

    ats_rpc_event_for_crash(pc, lr);

    for (;;)
    {
        __asm volatile ("wfi\n");
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "bl FaultHandler\n"
    );
}

static void ats_main_task(void *args)
{
    (void)args;
    ats_main();
    vTaskDelete(NULL);
}

void SystemInit(void)
{
}

int __attribute__((weak)) main(void)
{
    ats_rpc_init();

    if (xTaskCreate(ats_main_task, "Main", 32768U / sizeof(StackType_t), NULL, tskIDLE_PRIORITY + 2U, NULL) != pdPASS)
    {
        for (;;)
        {
            __asm volatile ("wfi\n");
        }
    }

    vTaskStartScheduler();

    for (;;)
    {
        __asm volatile ("wfi\n");
    }
}

void __attribute__((weak)) ats_main(void)
{
    ats_log_printf("Function `ats_main` not define, SDK `ats_main` is running...");

    while (1)
    {
        ats_thread_sleep(1000);
    }
}
