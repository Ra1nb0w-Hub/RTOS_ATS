#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#ifndef ATS_RTOS_RAM_BYTES
#define ATS_RTOS_RAM_BYTES                 (24U * 1024U)
#endif

#ifndef ATS_CPU_CLOCK_HZ
#define ATS_CPU_CLOCK_HZ                   (25000000UL)
#endif

#define configUSE_PREEMPTION               1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_IDLE_HOOK                0
#define configUSE_TICK_HOOK                0
#define configCPU_CLOCK_HZ                 ( ( uint32_t ) ATS_CPU_CLOCK_HZ )
#define configTICK_RATE_HZ                 ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES               5
#define configMINIMAL_STACK_SIZE           ( ( uint16_t ) 128 )
#define configTOTAL_HEAP_SIZE              ( ( size_t ) ATS_RTOS_RAM_BYTES )
#define configMAX_TASK_NAME_LEN            16
#define configUSE_TRACE_FACILITY           1
#define configRECORD_STACK_HIGH_ADDRESS    1
#define configGENERATE_RUN_TIME_STATS      0
#define configRUN_TIME_COUNTER_TYPE        uint32_t
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configIDLE_SHOULD_YIELD            1
#define configUSE_MUTEXES                  1
#define configQUEUE_REGISTRY_SIZE          0
#define configCHECK_FOR_STACK_OVERFLOW     0
#define configUSE_RECURSIVE_MUTEXES        0
#define configUSE_MALLOC_FAILED_HOOK       0
#define configUSE_APPLICATION_TASK_TAG     0
#define configUSE_COUNTING_SEMAPHORES      1
#define configUSE_TIMERS                   0
#define configSUPPORT_DYNAMIC_ALLOCATION   1
#define configSUPPORT_STATIC_ALLOCATION    0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configMESSAGE_BUFFER_LENGTH_TYPE   size_t
#define configTICK_TYPE_WIDTH_IN_BITS      TICK_TYPE_WIDTH_32_BITS

#define INCLUDE_vTaskPrioritySet           0
#define INCLUDE_uxTaskPriorityGet          0
#define INCLUDE_vTaskDelete                1
#define INCLUDE_vTaskSuspend               1
#define INCLUDE_xResumeFromISR             0
#define INCLUDE_vTaskDelayUntil            1
#define INCLUDE_vTaskDelay                 1
#define INCLUDE_xTaskGetSchedulerState     1
#define INCLUDE_xTaskGetIdleTaskHandle     1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_uxTaskGetNumberOfTasks     1

#define configPRIO_BITS                    3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define xPortPendSVHandler                 PendSV_Handler
#define vPortSVCHandler                    SVC_Handler
#define xPortSysTickHandler                SysTick_Handler

#define configASSERT(x)                    if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) {} }

#endif
