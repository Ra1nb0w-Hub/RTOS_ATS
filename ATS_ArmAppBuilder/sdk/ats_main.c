#include <stddef.h>

#include "ats_sys.h"
#include "ats_rpc.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"

static void send_crash_event(uint32_t pc)
{
    uint8_t frame[ATS_RPC_HEADER_SIZE + 4U] = {0};

    frame[0] = ATS_RPC_SOF0;
    frame[1] = ATS_RPC_SOF1;
    frame[2] = ATS_RPC_FRAME_TYPE_EVENT;
    frame[3] = ATS_RPC_SERVICE_CORE;
    frame[4] = ATS_RPC_CORE_CRASH;
    frame[5] = 4U;
    frame[6] = 0U;
    frame[7] = (uint8_t)(pc & 0xFFU);
    frame[8] = (uint8_t)((pc >> 8) & 0xFFU);
    frame[9] = (uint8_t)((pc >> 16) & 0xFFU);
    frame[10] = (uint8_t)((pc >> 24) & 0xFFU);

    (void)ats_rpc_transport_write(frame, ATS_RPC_HEADER_SIZE + 4);
}

__attribute__((noreturn)) void FaultHandler(uint32_t *sp)
{
    uint32_t pc = sp[6];

    send_crash_event(pc);

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
    if (xTaskCreate(ats_main_task,
                    "Main",
                    8192U / sizeof(StackType_t),
                    NULL,
                    tskIDLE_PRIORITY + 2U,
                    NULL) != pdPASS)
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
