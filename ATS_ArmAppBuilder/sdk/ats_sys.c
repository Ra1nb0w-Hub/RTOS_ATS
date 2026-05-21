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
#define ATS_TRACKED_THREAD_MAX      16U
#define ATS_WAIT_FOREVER_MS         0xFFFFFFFFU

typedef struct
{
    void (*func)(void *args);
    void *args;
} ats_thread_context_t;

static ats_log_callback_t s_log_callback = NULL;
static ats_keypad_event_t s_keypad_event = { ATS_KEY_CODE_NONE, false };
static char s_serial_number[ATS_SERIAL_NUMBER_SIZE] = { 0 };
static TaskHandle_t s_tracked_threads[ATS_TRACKED_THREAD_MAX] = { 0 };
static uint32_t s_random_state = 0x13572468UL;
static ats_datetime_t s_datetime_base;
static uint32_t s_datetime_base_tick_ms = 0U;
static bool s_datetime_valid = false;

void __attribute__((weak)) ats_platform_log_output(const char *level, const char *message)
{
    (void)ats_rpc_log_event(level, message);
}

static bool ats_is_leap_year(unsigned int year)
{
    if ((year % 400U) == 0U)
    {
        return true;
    }

    if ((year % 100U) == 0U)
    {
        return false;
    }

    return (year % 4U) == 0U;
}

static unsigned int ats_days_in_month(unsigned int year, unsigned int month)
{
    static const unsigned char s_days_per_month[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if ((month < 1U) || (month > 12U))
    {
        return 30U;
    }

    if ((month == 2U) && ats_is_leap_year(year))
    {
        return 29U;
    }

    return s_days_per_month[month - 1U];
}

static uint64_t ats_datetime_to_unix_seconds(const ats_datetime_t *datetime)
{
    uint64_t days = 0U;
    unsigned int year;
    unsigned int month;

    if (datetime == NULL)
    {
        return 0U;
    }

    if ((datetime->uiMonth < 1U) || (datetime->uiMonth > 12U) ||
        (datetime->uiDay < 1U) || (datetime->uiDay > 31U))
    {
        return 0U;
    }

    for (year = 1970U; year < datetime->uiYear; ++year)
    {
        days += ats_is_leap_year(year) ? 366U : 365U;
    }

    for (month = 1U; month < datetime->uiMonth; ++month)
    {
        days += ats_days_in_month(datetime->uiYear, month);
    }

    days += (uint64_t)(datetime->uiDay - 1U);

    return (((days * 24U) + datetime->uiHour) * 60U + datetime->uiMinute) * 60U + datetime->uiSecond;
}

static void ats_unix_seconds_to_datetime(uint64_t seconds, ats_datetime_t *datetime)
{
    uint64_t days;
    unsigned int year = 1970U;
    unsigned int month = 1U;

    if (datetime == NULL)
    {
        return;
    }

    days = seconds / 86400U;
    seconds %= 86400U;

    while (days >= (uint64_t)(ats_is_leap_year(year) ? 366U : 365U))
    {
        days -= ats_is_leap_year(year) ? 366U : 365U;
        ++year;
    }

    while (days >= (uint64_t)ats_days_in_month(year, month))
    {
        days -= ats_days_in_month(year, month);
        ++month;
    }

    datetime->uiYear = year;
    datetime->uiMonth = month;
    datetime->uiDay = (unsigned int)days + 1U;
    datetime->uiHour = (unsigned int)(seconds / 3600U);
    seconds %= 3600U;
    datetime->uiMinute = (unsigned int)(seconds / 60U);
    datetime->uiSecond = (unsigned int)(seconds % 60U);
}

static bool ats_month_from_abbrev(const char *text, unsigned int *month)
{
    static const char * const s_months[12] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    unsigned int index;

    if ((text == NULL) || (month == NULL))
    {
        return false;
    }

    for (index = 0U; index < 12U; ++index)
    {
        if ((text[0] == s_months[index][0]) &&
            (text[1] == s_months[index][1]) &&
            (text[2] == s_months[index][2]))
        {
            *month = index + 1U;
            return true;
        }
    }

    return false;
}

static void ats_seed_datetime_from_build(void)
{
    unsigned int month = 1U;
    unsigned int day = 1U;
    unsigned int year = 1970U;
    unsigned int hour = 0U;
    unsigned int minute = 0U;
    unsigned int second = 0U;

    if (s_datetime_valid)
    {
        return;
    }

    (void)ats_month_from_abbrev(__DATE__, &month);
    day = (unsigned int)(((__DATE__[4] == ' ') ? 0 : (__DATE__[4] - '0')) * 10U + (unsigned int)(__DATE__[5] - '0'));
    year = (unsigned int)((__DATE__[7] - '0') * 1000U +
                          (__DATE__[8] - '0') * 100U +
                          (__DATE__[9] - '0') * 10U +
                          (__DATE__[10] - '0'));
    hour = (unsigned int)((__TIME__[0] - '0') * 10U + (__TIME__[1] - '0'));
    minute = (unsigned int)((__TIME__[3] - '0') * 10U + (__TIME__[4] - '0'));
    second = (unsigned int)((__TIME__[6] - '0') * 10U + (__TIME__[7] - '0'));

    s_datetime_base.uiYear = year;
    s_datetime_base.uiMonth = month;
    s_datetime_base.uiDay = day;
    s_datetime_base.uiHour = hour;
    s_datetime_base.uiMinute = minute;
    s_datetime_base.uiSecond = second;
    s_datetime_base_tick_ms = 0U;
    s_datetime_valid = true;
}

static uint32_t ats_get_tick_ms_internal(void)
{
    const TickType_t ticks = xTaskGetTickCount();
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static void ats_register_thread(TaskHandle_t handle)
{
    UBaseType_t index;

    if (handle == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    for (index = 0U; index < ATS_TRACKED_THREAD_MAX; ++index)
    {
        if (s_tracked_threads[index] == NULL)
        {
            s_tracked_threads[index] = handle;
            break;
        }
    }
    taskEXIT_CRITICAL();
}

static void ats_unregister_thread(TaskHandle_t handle)
{
    UBaseType_t index;

    if (handle == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    for (index = 0U; index < ATS_TRACKED_THREAD_MAX; ++index)
    {
        if (s_tracked_threads[index] == handle)
        {
            s_tracked_threads[index] = NULL;
            break;
        }
    }
    taskEXIT_CRITICAL();
}

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

    ats_unregister_thread(xTaskGetCurrentTaskHandle());
    vTaskDelete(NULL);
}

void ats_log_set_callback(ats_log_callback_t callback)
{
    s_log_callback = callback;
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

void ats_log_print(const char *level, const char *string)
{
    if ((level == NULL) || (string == NULL))
    {
        return;
    }

    if (s_log_callback != NULL)
    {
        s_log_callback(level, string);
    }

    ats_platform_log_output(level, string);
}

void ats_log_printf(const char *level, const char *format, ...)
{
    char buffer[256];
    va_list args;

    if ((level == NULL) || (format == NULL))
    {
        return;
    }

    va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ats_log_print(level, buffer);
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

    ats_register_thread(task_handle);

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

int ats_thread_kill_all(void)
{
    TaskHandle_t handles[ATS_TRACKED_THREAD_MAX];
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    int count = 0;
    UBaseType_t index;

    taskENTER_CRITICAL();
    for (index = 0U; index < ATS_TRACKED_THREAD_MAX; ++index)
    {
        handles[index] = s_tracked_threads[index];
        s_tracked_threads[index] = NULL;
    }
    taskEXIT_CRITICAL();

    for (index = 0U; index < ATS_TRACKED_THREAD_MAX; ++index)
    {
        if ((handles[index] != NULL) && (handles[index] != current))
        {
            vTaskDelete(handles[index]);
            ++count;
        }
    }

    return count;
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
    uint64_t timestamp;

    if (datetime == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    ats_seed_datetime_from_build();
    timestamp = ats_datetime_to_unix_seconds(&s_datetime_base);
    timestamp += (uint64_t)((ats_get_tick_ms_internal() - s_datetime_base_tick_ms) / 1000U);
    ats_unix_seconds_to_datetime(timestamp, datetime);
    return ATS_EC_OK;
}

int ats_datetime_set(ats_datetime_t *datetime)
{
    if (datetime == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    s_datetime_base = *datetime;
    s_datetime_base_tick_ms = ats_get_tick_ms_internal();
    s_datetime_valid = true;
    return ATS_EC_OK;
}

unsigned long ats_timestamp_get(void)
{
    ats_seed_datetime_from_build();
    return (unsigned long)(ats_datetime_to_unix_seconds(&s_datetime_base) +
                           (uint64_t)((ats_get_tick_ms_internal() - s_datetime_base_tick_ms) / 1000U));
}

unsigned int ats_tick_get(void)
{
    return ats_get_tick_ms_internal();
}

int ats_random(unsigned int len, unsigned char *output)
{
    unsigned int index;

    if ((output == NULL) || (len == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    taskENTER_CRITICAL();
    s_random_state ^= ats_get_tick_ms_internal() + 0x9E3779B9UL;
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
    return s_serial_number;
}

int ats_serial_number_set(char *serial_number)
{
    if (serial_number == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    strncpy(s_serial_number, serial_number, ATS_SERIAL_NUMBER_SIZE - 1U);
    s_serial_number[ATS_SERIAL_NUMBER_SIZE - 1U] = '\0';
    return ATS_EC_OK;
}

void vApplicationMallocFailedHook(void)
{
    ats_log_print(ATS_LOG_LEVEL_ERROR, "FreeRTOS malloc failed.");
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    ats_log_printf(ATS_LOG_LEVEL_ERROR, "FreeRTOS stack overflow: %s", (task_name != NULL) ? task_name : "unknown");
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
