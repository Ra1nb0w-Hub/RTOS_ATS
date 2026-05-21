#include <stddef.h>

#include "ats_sys.h"
#include "ats_rpc.h"
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/portable.h"

#ifndef ATS_DEMO_HEAP_BYTES
#define ATS_DEMO_HEAP_BYTES        (24U * 1024U)
#endif

#ifndef ATS_DEMO_MAIN_STACK_WORDS
#define ATS_DEMO_MAIN_STACK_WORDS  512U
#endif

static uint8_t g_freertos_heap[ATS_DEMO_HEAP_BYTES] __attribute__((aligned(8)));

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
    static const HeapRegion_t heap_regions[] =
    {
        { g_freertos_heap, sizeof(g_freertos_heap) },
        { NULL, 0U }
    };

    vPortDefineHeapRegions(heap_regions);
    (void)ats_rpc_init_default();

    if (xTaskCreate(ats_main_task,
                    "ats_main",
                    ATS_DEMO_MAIN_STACK_WORDS,
                    NULL,
                    tskIDLE_PRIORITY + 2U,
                    NULL) != pdPASS)
    {
        for (;;)
        {
        }
    }

    vTaskStartScheduler();

    for (;;)
    {
    }
}

void __attribute__((weak)) ats_main(void)
{
    ats_log_printf(ATS_LOG_LEVEL_INFO, "Function `ats_main` not define, SDK `ats_main` is running...");

    while (1)
    {
        ats_thread_sleep(1000);
    }
}
