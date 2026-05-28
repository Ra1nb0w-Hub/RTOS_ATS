#include "ats_sys.h"
#include "ats_error.h"
#include "ats_rpc.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include "FreeRTOS/queue.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/task.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ATS_SERIAL_NUMBER_SIZE      32U
#define ATS_WAIT_FOREVER_MS         0xFFFFFFFFU

typedef struct
{
    void (*func)(void *args);
    void *args;
} ats_thread_context_t;

static ats_keypad_event_t s_keypad_event = { ATS_KEY_CODE_NONE, false };
static uint32_t s_random_state = 0x13572468UL;

static UBaseType_t ats_priority_to_freertos(ats_thread_priority_t priority)
{
    switch (priority)
    {
    case ATS_THREAD_PRIORITY_LOWEST:
        return tskIDLE_PRIORITY;
    case ATS_THREAD_PRIORITY_LOW:
        return tskIDLE_PRIORITY + 1U;
    case ATS_THREAD_PRIORITY_NORMAL:
        return tskIDLE_PRIORITY + 2U;
    case ATS_THREAD_PRIORITY_HIGH:
        return tskIDLE_PRIORITY + 3U;
    case ATS_THREAD_PRIORITY_HIGHEST:
    default:
        return tskIDLE_PRIORITY + 4U;
    }
}

static void ats_thread_entry(void *args)
{
    ats_thread_context_t *context = (ats_thread_context_t *)args;
    void (*func)(void *args_value) = NULL;
    void *func_args = NULL;

    if (context != NULL)
    {
        func = context->func;
        func_args = context->args;
        vPortFree(context);
    }

    if (func != NULL)
    {
        func(func_args);
    }

    vTaskDelete(NULL);
}

void *ats_malloc(unsigned int size)
{
    if (size == 0U)
    {
        return NULL;
    }

    return pvPortMalloc((size_t)size);
}

void ats_free(void *ptr)
{
    if (ptr != NULL)
    {
        vPortFree(ptr);
    }
}

void ats_log_print(const char *string)
{
    if (string == NULL)
    {
        return;
    }

    (void)ats_rpc_event(ATS_RPC_SERVICE_CORE, ATS_RPC_CORE_WRITE_LOG, (const uint8_t *)string, (uint16_t)strlen(string));
}

void ats_log_printf(const char *format, ...)
{
    char buffer[256];
    va_list args;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ats_log_print(buffer);
}

int ats_keypad_set_event(uint8_t keyCode, bool status)
{
    taskENTER_CRITICAL();
    s_keypad_event.key_code = (ats_key_code_t)keyCode;
    s_keypad_event.press_status = status;
    taskEXIT_CRITICAL();
    return ATS_EC_OK;
}

int ats_keypad_get_event(ats_keypad_event_t *event)
{
    if (event == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    taskENTER_CRITICAL();
    *event = s_keypad_event;
    taskEXIT_CRITICAL();
    return ATS_EC_OK;
}

int ats_thread_create(ats_thread_handle_t *handle, const char *name,
                      ats_thread_priority_t priority,
                      unsigned int stackSize,
                      void (*func)(void *args), void *args)
{
    ats_thread_context_t *context;
    TaskHandle_t task_handle = NULL;
    const char *task_name = (name != NULL) ? name : "ats_task";
    const configSTACK_DEPTH_TYPE stack_words =
        (stackSize == 0U) ? configMINIMAL_STACK_SIZE :
        (configSTACK_DEPTH_TYPE)((stackSize + sizeof(StackType_t) - 1U) / sizeof(StackType_t));

    if (func == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    context = (ats_thread_context_t *)pvPortMalloc(sizeof(*context));
    if (context == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    context->func = func;
    context->args = args;

    if (xTaskCreate(ats_thread_entry,
                    task_name,
                    stack_words,
                    context,
                    ats_priority_to_freertos(priority),
                    &task_handle) != pdPASS)
    {
        vPortFree(context);
        return ATS_EC_INVALID_PARAM;
    }

    if (handle != NULL)
    {
        *handle = (ats_thread_handle_t)task_handle;
    }

    return ATS_EC_OK;
}

int ats_thread_sleep(unsigned int ms)
{
    const TickType_t delay_ticks = (ms == 0U) ? 0U : pdMS_TO_TICKS(ms);

    if (delay_ticks == 0U)
    {
        taskYIELD();
    }
    else
    {
        vTaskDelay(delay_ticks);
    }

    return ATS_EC_OK;
}

int ats_thread_info(char *buffer, size_t buffer_size)
{
    TaskStatus_t *task_status;
    UBaseType_t task_count;
    UBaseType_t index;
    size_t offset = 0U;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    buffer[0] = '\0';

    task_count = uxTaskGetNumberOfTasks();
    if (task_count == 0U)
    {
        return ATS_EC_OK;
    }

    task_status = (TaskStatus_t *)pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (task_status == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    task_count = uxTaskGetSystemState(task_status, task_count, NULL);

    for (index = 0U; index < task_count; ++index)
    {
        const char *name = task_status[index].pcTaskName;
        const uint32_t remaining_bytes = (uint32_t)(task_status[index].usStackHighWaterMark * sizeof(StackType_t));
        const uint32_t stack_size = (uint32_t)((task_status[index].pxEndOfStack - task_status[index].pxStackBase + 2) * sizeof(StackType_t));
        int written;

        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s,%lu,%lu\n",
                           (name != NULL) ? name : "unknown",
                           (unsigned long)remaining_bytes,
                           (unsigned long)stack_size);

        if ((written < 0) || ((size_t)written >= buffer_size - offset))
        {
            vPortFree(task_status);
            return ATS_EC_INVALID_PARAM;
        }

        offset += (size_t)written;
    }

    vPortFree(task_status);

    return ATS_EC_OK;
}

int ats_mutex_create(ats_mutex_handle_t *handle, const char *name)
{
    SemaphoreHandle_t mutex;

    (void)name;

    if (handle == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    *handle = (ats_mutex_handle_t)mutex;
    return ATS_EC_OK;
}

int ats_mutex_lock(ats_mutex_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xSemaphoreTake((SemaphoreHandle_t)(*handle), portMAX_DELAY) == pdTRUE) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

int ats_mutex_unlock(ats_mutex_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xSemaphoreGive((SemaphoreHandle_t)(*handle)) == pdTRUE) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

int ats_semaphore_create(ats_semaphore_handle_t *handle, const char *name, unsigned int count)
{
    const UBaseType_t max_count = (count == 0U) ? 1U : (UBaseType_t)count;
    const UBaseType_t initial_count = (UBaseType_t)count;
    SemaphoreHandle_t semaphore;

    (void)name;

    if (handle == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    semaphore = xSemaphoreCreateCounting(max_count, initial_count);
    if (semaphore == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    *handle = (ats_semaphore_handle_t)semaphore;
    return ATS_EC_OK;
}

int ats_semaphore_wait(ats_semaphore_handle_t *handle, unsigned int timeout)
{
    TickType_t wait_ticks;

    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    wait_ticks = (timeout == ATS_WAIT_FOREVER_MS) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    return (xSemaphoreTake((SemaphoreHandle_t)(*handle), wait_ticks) == pdTRUE) ? ATS_EC_OK : ATS_EC_TIMEOUT;
}

int ats_semaphore_post(ats_semaphore_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xSemaphoreGive((SemaphoreHandle_t)(*handle)) == pdTRUE) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

static int datetime_encode(const ats_datetime_t *dt, uint8_t *buf)
{
    buf[0] = (uint8_t)(dt->uiYear & 0xFFU);
    buf[1] = (uint8_t)((dt->uiYear >> 8) & 0xFFU);
    buf[2] = (uint8_t)dt->uiMonth;
    buf[3] = (uint8_t)dt->uiDay;
    buf[4] = (uint8_t)dt->uiHour;
    buf[5] = (uint8_t)dt->uiMinute;
    buf[6] = (uint8_t)dt->uiSecond;
    return ATS_EC_OK;
}

static int datetime_decode(ats_datetime_t *dt, const uint8_t *buf, uint16_t len)
{
    if (len < 7U)
    {
        return ATS_EC_INVALID_PARAM;
    }

    dt->uiYear   = (unsigned int)buf[0] | ((unsigned int)buf[1] << 8);
    dt->uiMonth  = (unsigned int)buf[2];
    dt->uiDay    = (unsigned int)buf[3];
    dt->uiHour   = (unsigned int)buf[4];
    dt->uiMinute = (unsigned int)buf[5];
    dt->uiSecond = (unsigned int)buf[6];
    return ATS_EC_OK;
}

int ats_datetime_get(ats_datetime_t *datetime)
{
    uint8_t resp_buf[7];
    uint16_t resp_len = sizeof(resp_buf);
    int status;

    if (datetime == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    status = ats_rpc_request(ATS_RPC_SERVICE_CORE,
                             ATS_RPC_CORE_GET_DATETIME,
                             NULL, 0U,
                             resp_buf, &resp_len,
                             2000U);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    return datetime_decode(datetime, resp_buf, resp_len);
}

int ats_datetime_set(ats_datetime_t *datetime)
{
    uint8_t req_buf[7];
    uint8_t resp_buf[1];
    uint16_t resp_len = sizeof(resp_buf);

    if (datetime == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    (void)datetime_encode(datetime, req_buf);

    return ats_rpc_request(ATS_RPC_SERVICE_CORE,
                           ATS_RPC_CORE_SET_DATETIME,
                           req_buf, sizeof(req_buf),
                           resp_buf, &resp_len,
                           2000U);
}

unsigned long ats_timestamp_get(void)
{
    uint8_t resp_buf[4];
    uint16_t resp_len = sizeof(resp_buf);
    int status;
    uint32_t timestamp;

    status = ats_rpc_request(ATS_RPC_SERVICE_CORE,
                             ATS_RPC_CORE_GET_TIMESTAMP,
                             NULL, 0U,
                             resp_buf, &resp_len,
                             2000U);
    if ((status != ATS_EC_OK) || (resp_len < 4U))
    {
        return 0UL;
    }

    timestamp = (uint32_t)resp_buf[0]
              | ((uint32_t)resp_buf[1] << 8)
              | ((uint32_t)resp_buf[2] << 16)
              | ((uint32_t)resp_buf[3] << 24);

    return (unsigned long)timestamp;
}

unsigned int ats_tick_get(void)
{
    const TickType_t ticks = xTaskGetTickCount();

    return (unsigned int)((ticks * 1000U) / configTICK_RATE_HZ);
}

int ats_random(unsigned int len, unsigned char *output)
{
    unsigned int index;

    if ((output == NULL) || (len == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    taskENTER_CRITICAL();
    s_random_state ^= ats_tick_get() + 0x9E3779B9UL;
    for (index = 0U; index < len; ++index)
    {
        s_random_state ^= s_random_state << 13;
        s_random_state ^= s_random_state >> 17;
        s_random_state ^= s_random_state << 5;
        output[index] = (unsigned char)(s_random_state & 0xFFU);
    }
    taskEXIT_CRITICAL();

    return ATS_EC_OK;
}

char *ats_serial_number_get(void)
{
    static char s_sn[ATS_SERIAL_NUMBER_SIZE];
    uint8_t resp_buf[ATS_SERIAL_NUMBER_SIZE - 1U];
    uint16_t resp_len = sizeof(resp_buf);
    int status;

    status = ats_rpc_request(ATS_RPC_SERVICE_CORE,
                             ATS_RPC_CORE_GET_SERIAL_NUMBER,
                             NULL, 0U,
                             resp_buf, &resp_len,
                             2000U);
    if ((status == ATS_EC_OK) && (resp_len > 0U))
    {
        const uint16_t copy_len =
            (resp_len < (ATS_SERIAL_NUMBER_SIZE - 1U))
            ? resp_len : (ATS_SERIAL_NUMBER_SIZE - 1U);
        (void)memcpy(s_sn, resp_buf, copy_len);
        s_sn[copy_len] = '\0';
    }
    else
    {
        s_sn[0] = '\0';
    }

    return s_sn;
}
