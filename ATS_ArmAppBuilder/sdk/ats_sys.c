#include "ats_sys.h"
#include "ats_error.h"
#include "ats_rpc.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include "FreeRTOS/queue.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/timers.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ATS_SERIAL_NUMBER_SIZE      32U
#define ATS_WAIT_FOREVER_MS         0xFFFFFFFFU

#define IsLeepYear(year) (((year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0))) ? true : false)

typedef struct
{
    void (*func)(void *args);
    void *args;
} ats_thread_context_t;

typedef struct
{
    void (*callback)(void *args);
    void *args;
} ats_timer_context_t;

static unsigned long s_timestamp = 0UL;
static unsigned int s_timestamp_tick = 0U;
static ats_keypad_event_t s_keypad_event = { ATS_KEY_CODE_NONE, false };
static uint32_t s_random_state = 0x13572468UL;

static UBaseType_t _priority_to_freertos(ats_thread_priority_t priority)
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

static void _thread_entry(void *args)
{
    ats_thread_context_t *context = (ats_thread_context_t *)args;
    void (*func)(void *args_value) = NULL;
    void *func_args = NULL;

    if (context != NULL)
    {
        func = context->func;
        func_args = context->args;
        ats_free(context);
    }

    if (func != NULL)
    {
        func(func_args);
    }

    vTaskDelete(NULL);
}

static void _timer_entry(TimerHandle_t xTimer)
{
    ats_timer_context_t *context = (ats_timer_context_t *)pvTimerGetTimerID(xTimer);

    if (context != NULL)
    {
        if (context->callback != NULL)
        {
            context->callback(context->args);
        }
    }
}

static void _TimestampConvertDateTime(unsigned long ulTimestamp, ats_datetime_t *pstDateTime)
{
    unsigned int uiRemainDays = 0;

    // 计算秒数
    pstDateTime->uiSecond = ulTimestamp % 60;
    ulTimestamp /= 60;

    // 计算分钟
    pstDateTime->uiMinute = ulTimestamp % 60;
    ulTimestamp /= 60;

    // 计算小时
    pstDateTime->uiHour = ulTimestamp % 24;
    ulTimestamp /= 24;

    // 计算年、月、日
    uiRemainDays = ulTimestamp;
    pstDateTime->uiYear = 1970;
    pstDateTime->uiMonth = 1;
    pstDateTime->uiDay = 1;
    while (uiRemainDays > 0)
    {
        unsigned int uiDaysInYear = 365;

        if (IsLeepYear(pstDateTime->uiYear))
            uiDaysInYear += 1;

        if (uiRemainDays >= uiDaysInYear)
        {
            uiRemainDays -= uiDaysInYear;
            pstDateTime->uiYear++;
        }
        else
        {
            unsigned int uiDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

            for (int i = 0; i < 12; i++)
            {
                // 需要考虑当前年份是否是闰年
                if ((i == 1) && IsLeepYear(pstDateTime->uiYear))
                    uiDaysInMonth[1] = 29;

                if (uiRemainDays >= uiDaysInMonth[i])
                {
                    uiRemainDays -= uiDaysInMonth[i];
                    pstDateTime->uiMonth++;
                }
                else
                {
                    pstDateTime->uiDay += uiRemainDays;
                    break;
                }
            }
            break;
        }
    }
}

/**
 * @brief 日期和时间转换为时间戳
 * 
 * @param[in] pstDateTime 日期和时间结构体
 * @param[in] iTimezone 时区
 * @param[out] pulTimestamp 时间戳
 * 
 * @return 0:成功 <0:失败
 */
static void _DateTimeConvertTimestamp(ats_datetime_t *pstDateTime, int iTimezone, unsigned long *pulTimestamp)
{
    char cDaysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // 计算年份偏移量
    for (unsigned int i = 1970; i < pstDateTime->uiYear; i++)
    {
        if (IsLeepYear(i) == true)
            *pulTimestamp += 366 * 24 * 60 * 60; // 闰年366天
        else
            *pulTimestamp += 365 * 24 * 60 * 60; // 平年365天
    }

    // 需要考虑当前年份是否是闰年
    if (IsLeepYear(pstDateTime->uiYear))
        cDaysInMonth[2] = 29;

    // 加上月份总天数
    for (unsigned int i = 1; i < pstDateTime->uiMonth; i++)
        *pulTimestamp += cDaysInMonth[i] * 24 * 60 * 60;

    // 加上日期天数
    *pulTimestamp += (pstDateTime->uiDay - 1) * 24 * 60 * 60;

    // 加上时间
    *pulTimestamp += (pstDateTime->uiHour + iTimezone) * 60 * 60;
    *pulTimestamp += pstDateTime->uiMinute * 60;
    *pulTimestamp += pstDateTime->uiSecond;
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

    context = (ats_thread_context_t *)ats_malloc(sizeof(*context));
    if (context == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    context->func = func;
    context->args = args;

    if (xTaskCreate(_thread_entry,
                    task_name,
                    stack_words,
                    context,
                    _priority_to_freertos(priority),
                    &task_handle) != pdPASS)
    {
        ats_free(context);
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

    task_status = (TaskStatus_t *)ats_malloc(task_count * sizeof(TaskStatus_t));
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
            ats_free(task_status);
            return ATS_EC_INVALID_PARAM;
        }

        offset += (size_t)written;
    }

    ats_free(task_status);

    return ATS_EC_OK;
}

int ats_heap_info(uint32_t *used, uint32_t *total)
{
    if ((used == NULL) || (total == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    size_t free_bytes = xPortGetFreeHeapSize();

    *total = (uint32_t)configTOTAL_HEAP_SIZE;
    *used = (uint32_t)(configTOTAL_HEAP_SIZE - free_bytes);

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

int ats_datetime_get(ats_datetime_t *datetime)
{
    _TimestampConvertDateTime(ats_timestamp_get(), datetime);
    return ATS_EC_OK;
}

int ats_datetime_set(ats_datetime_t *datetime)
{
    unsigned long ulTimestamp = 0U;

    _DateTimeConvertTimestamp(datetime, 0, &ulTimestamp);
    s_timestamp = ulTimestamp;
    s_timestamp_tick = ats_tick_get();
    return ATS_EC_OK;
}

unsigned long ats_timestamp_get(void)
{
    uint8_t resp_buf[4];
    uint16_t resp_len = sizeof(resp_buf);
    int status;

    if (s_timestamp)
        return s_timestamp + ((ats_tick_get() - s_timestamp_tick) / 1000U);

    status = ats_rpc_request(ATS_RPC_SERVICE_CORE,
                             ATS_RPC_CORE_GET_TIMESTAMP,
                             NULL, 0U,
                             resp_buf, &resp_len,
                             2000U);
    if ((status != ATS_EC_OK) || (resp_len < 4U))
        return s_timestamp;

    s_timestamp = (unsigned long)ats_rpc_read_u32_le(resp_buf);
    s_timestamp_tick = ats_tick_get();
    return s_timestamp;
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

int ats_timer_create(ats_timer_handle_t *handle, const char *name,
                     ats_timer_type_t type, unsigned int period_ms,
                     void (*callback)(void *args), void *args)
{
    ats_timer_context_t *context;
    TimerHandle_t timer;
    const char *timer_name = (name != NULL) ? name : "ats_timer";
    const UBaseType_t auto_reload = (type == ATS_TIMER_PERIODIC) ? pdTRUE : pdFALSE;

    if ((handle == NULL) || (callback == NULL) || (period_ms == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    context = (ats_timer_context_t *)ats_malloc(sizeof(*context));
    if (context == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    context->callback = callback;
    context->args = args;

    timer = xTimerCreate(timer_name,
                         pdMS_TO_TICKS(period_ms),
                         auto_reload,
                         (void *)context,
                         _timer_entry);
    if (timer == NULL)
    {
        ats_free(context);
        return ATS_EC_INVALID_PARAM;
    }

    *handle = (ats_timer_handle_t)timer;
    return ATS_EC_OK;
}

int ats_timer_start(ats_timer_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xTimerStart((TimerHandle_t)(*handle), 0U) == pdPASS) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

int ats_timer_stop(ats_timer_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xTimerStop((TimerHandle_t)(*handle), 0U) == pdPASS) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

int ats_timer_reset(ats_timer_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return (xTimerReset((TimerHandle_t)(*handle), 0U) == pdPASS) ? ATS_EC_OK : ATS_EC_INVALID_PARAM;
}

int ats_timer_delete(ats_timer_handle_t *handle)
{
    ats_timer_context_t *context;

    if ((handle == NULL) || (*handle == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    context = (ats_timer_context_t *)pvTimerGetTimerID((TimerHandle_t)(*handle));

    if (xTimerDelete((TimerHandle_t)(*handle), 0U) != pdPASS)
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (context != NULL)
    {
        ats_free(context);
    }

    *handle = NULL;
    return ATS_EC_OK;
}

bool ats_timer_is_running(ats_timer_handle_t *handle)
{
    if ((handle == NULL) || (*handle == NULL))
    {
        return false;
    }

    return (xTimerIsTimerActive((TimerHandle_t)(*handle)) != pdFALSE) ? true : false;
}
