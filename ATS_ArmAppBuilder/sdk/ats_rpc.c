#include "ats_rpc.h"
#include "ats_error.h"
#include "ats_sys.h"
#include "ats_net.h"
#include "ats_printer.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/task.h"
#include <string.h>
#include <stdio.h>

#define ATS_RPC_MAX_PENDING        4U

/* ------------------------------------------------------------------
 *  UART definitions — 4 channels
 * ------------------------------------------------------------------ */
static const uint32_t s_uart_base[ATS_RPC_CHANNEL_COUNT] = {
    0x40004000UL,  /* CH0: CTRL     */
    0x40005000UL,  /* CH1: DISPLAY  */
    0x40006000UL,  /* CH2: DATA     */
    0x40007000UL   /* CH3: LOG      */
};

#define ATS_UART_DATA_OFFSET       0x000UL
#define ATS_UART_STATE_OFFSET      0x004UL
#define ATS_UART_CTRL_OFFSET       0x008UL
#define ATS_UART_BAUDDIV_OFFSET    0x010UL

#define ATS_UART_STATE_TXFULL      (1UL << 0)
#define ATS_UART_STATE_RXFULL      (1UL << 1)
#define ATS_UART_CTRL_TX_EN        (1UL << 0)
#define ATS_UART_CTRL_RX_EN        (1UL << 1)

#define ATS_UART_TX_FIFO_DEPTH     16U
#define ATS_UART_BAUD_RATE         4608000UL
#define ATS_UART_BAUD_DIVISOR      (ATS_CPU_CLOCK_HZ / ATS_UART_BAUD_RATE)

/* ------------------------------------------------------------------
 *  channel routing — service+command → UART channel
 * ------------------------------------------------------------------ */
static ats_rpc_channel_t get_channel(uint8_t service, uint8_t command)
{
    /* Core WriteLog → dedicated log channel */
    if (service == ATS_RPC_SERVICE_CORE && command == ATS_RPC_CORE_WRITE_LOG)
    {
        return ATS_RPC_CHANNEL_LOG;
    }

    switch (service)
    {
    case ATS_RPC_SERVICE_CORE:
    case ATS_RPC_SERVICE_PRINTER:
    case ATS_RPC_SERVICE_AUDIO:
    case ATS_RPC_SERVICE_READER:
        return ATS_RPC_CHANNEL_CTRL;

    case ATS_RPC_SERVICE_LCD:
        return ATS_RPC_CHANNEL_DISPLAY;

    case ATS_RPC_SERVICE_NET:
    case ATS_RPC_SERVICE_FS:
        return ATS_RPC_CHANNEL_DATA;

    default:
        return ATS_RPC_CHANNEL_CTRL;
    }
}

/* ------------------------------------------------------------------
 *  pending request / service handler tables
 * ------------------------------------------------------------------ */

typedef struct
{
    uint8_t             service;
    uint8_t             command;
    uint8_t            *response_buf;
    uint16_t            response_buf_size;
    uint16_t           *response_len;
    int                 result;
    uint8_t             request_id;
    SemaphoreHandle_t   semaphore;
    bool                in_use;
} ats_rpc_pending_t;

static ats_net_rpc_callback_t     s_net_rpc_callback = {0};
static ats_printer_rpc_callback_t s_printer_rpc_callback = {0};
static ats_rpc_pending_t          s_pending[ATS_RPC_MAX_PENDING];
static ats_rpc_handler_t          s_handlers[ATS_RPC_MAX_SERVICES];
static bool                       s_rpc_initialized = false;
static uint8_t                    s_request_id_counter = 0U;
static SemaphoreHandle_t          s_pending_mutex = NULL;
static SemaphoreHandle_t          s_tx_mutex[ATS_RPC_CHANNEL_COUNT] = {NULL};

/* ------------------------------------------------------------------
 *  UART low-level helpers (base address per channel)
 * ------------------------------------------------------------------ */
static volatile uint32_t *uart_reg(uint32_t base, uint32_t offset)
{
    return (volatile uint32_t *)(base + offset);
}

static uint32_t uart_read(uint32_t base, uint32_t offset)
{
    return *uart_reg(base, offset);
}

static void uart_write(uint32_t base, uint32_t offset, uint32_t value)
{
    *uart_reg(base, offset) = value;
}

static void uart_init_tx(ats_rpc_channel_t channel)
{
    const uint32_t base = s_uart_base[channel];
    const uint32_t ctrl = uart_read(base, ATS_UART_CTRL_OFFSET);
    uart_write(base, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(base, ATS_UART_CTRL_OFFSET, ctrl | ATS_UART_CTRL_TX_EN);
}

static void uart_init_rx(ats_rpc_channel_t channel)
{
    const uint32_t base = s_uart_base[channel];
    const uint32_t ctrl = uart_read(base, ATS_UART_CTRL_OFFSET);
    uart_write(base, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(base, ATS_UART_CTRL_OFFSET, ctrl | ATS_UART_CTRL_TX_EN | ATS_UART_CTRL_RX_EN);
}

static bool uart_try_read_byte(ats_rpc_channel_t channel, uint8_t *byte)
{
    const uint32_t base = s_uart_base[channel];

    if (byte == NULL)
    {
        return false;
    }

    if ((uart_read(base, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_RXFULL) == 0U)
    {
        return false;
    }

    *byte = (uint8_t)(uart_read(base, ATS_UART_DATA_OFFSET) & 0xFFU);
    return true;
}

/* ------------------------------------------------------------------
 *  transport write — with per-channel TX mutex
 * ------------------------------------------------------------------ */
static int ats_rpc_transport_write(ats_rpc_channel_t channel, const uint8_t *data, uint16_t length)
{
    const uint32_t base = s_uart_base[channel];
    uint16_t index;
    uint16_t batch;

    if ((data == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (s_tx_mutex[channel] != NULL)
    {
        (void)xSemaphoreTake(s_tx_mutex[channel], portMAX_DELAY);
    }

    uart_init_tx(channel);

    index = 0U;
    while (index < length)
    {
        while (uart_read(base, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_TXFULL)
        {
        }

        batch = 0U;
        while ((batch < ATS_UART_TX_FIFO_DEPTH) && (index < length))
        {
            uart_write(base, ATS_UART_DATA_OFFSET, (uint32_t)data[index]);
            ++index;
            ++batch;
        }
    }

    if (s_tx_mutex[channel] != NULL)
    {
        (void)xSemaphoreGive(s_tx_mutex[channel]);
    }

    return ATS_EC_OK;
}

/* ------------------------------------------------------------------
 *  transport write direct — no mutex (for crash/ISR context)
 * ------------------------------------------------------------------ */
static void ats_rpc_transport_write_direct(ats_rpc_channel_t channel, const uint8_t *data, uint16_t length)
{
    const uint32_t base = s_uart_base[channel];
    uint16_t index = 0U;
    uint16_t batch;

    uart_init_tx(channel);

    while (index < length)
    {
        while (uart_read(base, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_TXFULL)
        {
        }

        batch = 0U;
        while ((batch < ATS_UART_TX_FIFO_DEPTH) && (index < length))
        {
            uart_write(base, ATS_UART_DATA_OFFSET, (uint32_t)data[index]);
            ++index;
            ++batch;
        }
    }
}

/* ------------------------------------------------------------------
 *  transport read — UART
 * ------------------------------------------------------------------ */
static int ats_rpc_transport_read(ats_rpc_channel_t channel, uint8_t *byte, uint32_t timeout_ms)
{
    uint32_t     start_tick = 0U;
    BaseType_t   scheduler_state;

    if (byte == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    uart_init_rx(channel);

    scheduler_state = xTaskGetSchedulerState();
    if (scheduler_state != taskSCHEDULER_NOT_STARTED)
    {
        start_tick = xTaskGetTickCount();
    }

    for (;;)
    {
        if (uart_try_read_byte(channel, byte))
        {
            return ATS_EC_OK;
        }

        if (timeout_ms == 0U)
        {
            return ATS_EC_TIMEOUT;
        }

        if (scheduler_state == taskSCHEDULER_NOT_STARTED)
        {
            volatile uint32_t delay;
            for (delay = 0U; delay < 2048U; ++delay)
            {
                __asm__ volatile ("nop");
            }
            continue;
        }

        if (((uint32_t)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS)) >= timeout_ms)
        {
            return ATS_EC_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

/* ==================================================================
 *  RPC protocol engine
 * ================================================================== */

void ats_rpc_write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

uint16_t ats_rpc_read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

void ats_rpc_write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

uint32_t ats_rpc_read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24);
}

static int read_exact(ats_rpc_channel_t channel, uint8_t *buffer, uint16_t length, uint32_t timeout_ms)
{
    uint16_t index;

    if ((buffer == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    for (index = 0U; index < length; ++index)
    {
        const int status = ats_rpc_transport_read(channel, &buffer[index], timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }
    }

    return ATS_EC_OK;
}

static int send_frame(uint8_t frame_type, uint8_t service, uint8_t command, uint8_t request_id, const uint8_t *payload, uint16_t payload_length)
{
    const uint16_t frame_length = (uint16_t)(ATS_RPC_HEADER_SIZE + payload_length);
    const ats_rpc_channel_t channel = get_channel(service, command);
    uint8_t *frame_buffer;
    int status;

    if ((payload == NULL) && (payload_length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (!s_rpc_initialized)
    {
        return ATS_EC_RPC_NOT_INIT;
    }

    frame_buffer = (uint8_t *)ats_malloc(frame_length);
    if (frame_buffer == NULL)
    {
        return ATS_EC_NO_MEMORY;
    }

    frame_buffer[0] = ATS_RPC_SOF0;
    frame_buffer[1] = ATS_RPC_SOF1;
    frame_buffer[2] = frame_type;
    frame_buffer[3] = service;
    frame_buffer[4] = command;
    frame_buffer[5] = request_id;
    ats_rpc_write_u16_le(&frame_buffer[6], payload_length);

    if (payload_length != 0U)
    {
        (void)memcpy(&frame_buffer[ATS_RPC_HEADER_SIZE], payload, payload_length);
    }

    status = ats_rpc_transport_write(channel, frame_buffer, frame_length);
    ats_free(frame_buffer);
    return status;
}

static int recv_frame(ats_rpc_channel_t channel, ats_rpc_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t header[ATS_RPC_HEADER_SIZE];
    uint16_t payload_length;
    uint8_t *data_buffer;
    int status;

    if (frame == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    for (;;)
    {
        status = read_exact(channel, &header[0], 1U, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if (header[0] != ATS_RPC_SOF0)
        {
            continue;
        }

        status = read_exact(channel, &header[1], 1U, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if (header[1] != ATS_RPC_SOF1)
        {
            continue;
        }

        break;
    }

    status = read_exact(channel, &header[2], (uint16_t)(ATS_RPC_HEADER_SIZE - 2U), timeout_ms);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    payload_length = ats_rpc_read_u16_le(&header[6]);

    if (payload_length != 0U)
    {
        data_buffer = (uint8_t *)ats_malloc(payload_length);
        if (data_buffer == NULL)
        {
            return ATS_EC_NO_MEMORY;
        }

        status = read_exact(channel, data_buffer, payload_length, timeout_ms);
        if (status != ATS_EC_OK)
        {
            ats_free(data_buffer);
            return status;
        }

        frame->payload = data_buffer;
    }
    else
    {
        frame->payload = NULL;
    }

    frame->frame_type     = header[2];
    frame->service        = header[3];
    frame->command        = header[4];
    frame->request_id     = header[5];
    frame->payload_length = payload_length;

    return ATS_EC_OK;
}

static void rpc_task(void *args)
{
    const ats_rpc_channel_t channel = (ats_rpc_channel_t)(uintptr_t)args;

    for (;;)
    {
        ats_rpc_frame_t frame;
        int status;

        (void)memset(&frame, 0, sizeof(frame));

        status = recv_frame(channel, &frame, portMAX_DELAY);
        if (status != ATS_EC_OK)
        {
            continue;
        }

        ats_rpc_dispatch(&frame);

        if (frame.payload != NULL)
        {
            ats_free(frame.payload);
        }
    }
}

int ats_rpc_event(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_EVENT, service, command, 0U, payload, payload_length);
}

void ats_rpc_event_for_crash(uint32_t pc, uint32_t lr)
{
    uint8_t frame[32U] = {0};

    if (!s_rpc_initialized)
    {
        return;
    }

    frame[0] = ATS_RPC_SOF0;
    frame[1] = ATS_RPC_SOF1;
    frame[2] = ATS_RPC_FRAME_TYPE_EVENT;
    frame[3] = ATS_RPC_SERVICE_CORE;
    frame[4] = ATS_RPC_CORE_CRASH;
    frame[5] = 0U;      /* request_id: Event 固定为 0 */
    frame[6] = 24U;
    frame[7] = 0U;

    /* PC [8..11] */
    frame[8]  = (uint8_t)(pc & 0xFFU);
    frame[9]  = (uint8_t)((pc >> 8) & 0xFFU);
    frame[10] = (uint8_t)((pc >> 16) & 0xFFU);
    frame[11] = (uint8_t)((pc >> 24) & 0xFFU);

    /* LR [12..15] */
    frame[12] = (uint8_t)(lr & 0xFFU);
    frame[13] = (uint8_t)((lr >> 8) & 0xFFU);
    frame[14] = (uint8_t)((lr >> 16) & 0xFFU);
    frame[15] = (uint8_t)((lr >> 24) & 0xFFU);

    /* CFSR [16..19] — 0xE000ED28 */
    {
        const uint32_t cfsr = *(const volatile uint32_t *)0xE000ED28U;
        frame[16] = (uint8_t)(cfsr & 0xFFU);         /* MMFSR */
        frame[17] = (uint8_t)((cfsr >> 8) & 0xFFU);   /* BFSR */
        frame[18] = (uint8_t)((cfsr >> 16) & 0xFFU);  /* UFSR */
        frame[19] = 0U;
    }

    /* HFSR [20..23] — 0xE000ED2C */
    {
        const uint32_t hfsr = *(const volatile uint32_t *)0xE000ED2CU;
        frame[20] = (uint8_t)(hfsr & 0xFFU);
        frame[21] = (uint8_t)((hfsr >> 8) & 0xFFU);
        frame[22] = (uint8_t)((hfsr >> 16) & 0xFFU);
        frame[23] = (uint8_t)((hfsr >> 24) & 0xFFU);
    }

    /* BFAR [24..27] — 0xE000ED38 */
    {
        const uint32_t bfar = *(const volatile uint32_t *)0xE000ED38U;
        frame[24] = (uint8_t)(bfar & 0xFFU);
        frame[25] = (uint8_t)((bfar >> 8) & 0xFFU);
        frame[26] = (uint8_t)((bfar >> 16) & 0xFFU);
        frame[27] = (uint8_t)((bfar >> 24) & 0xFFU);
    }

    /* MMFAR [28..31] — 0xE000ED34 */
    {
        const uint32_t mmfar = *(const volatile uint32_t *)0xE000ED34U;
        frame[28] = (uint8_t)(mmfar & 0xFFU);
        frame[29] = (uint8_t)((mmfar >> 8) & 0xFFU);
        frame[30] = (uint8_t)((mmfar >> 16) & 0xFFU);
        frame[31] = (uint8_t)((mmfar >> 24) & 0xFFU);
    }

    /* crash 上下文不使用 mutex，直接写入 CTRL 通道 */
    ats_rpc_transport_write_direct(ATS_RPC_CHANNEL_CTRL, frame, sizeof(frame));
}

int ats_rpc_response(uint8_t service, uint8_t command, uint8_t request_id, const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_RESPONSE, service, command, request_id, payload, payload_length);
}

int ats_rpc_request(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms)
{
    int slot = -1;
    int status;
    TickType_t wait_ticks;
    UBaseType_t i;
    uint8_t rid;

    if ((response_length == NULL) || ((response_payload == NULL) && (*response_length != 0U)))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (!s_rpc_initialized)
    {
        return ATS_EC_RPC_NOT_INIT;
    }

    (void)xSemaphoreTake(s_pending_mutex, portMAX_DELAY);

    /* 分配唯一 request_id: 1~255 循环 */
    s_request_id_counter++;
    if (s_request_id_counter == 0U)
    {
        s_request_id_counter = 1U;
    }
    rid = s_request_id_counter;

    for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
    {
        if (!s_pending[i].in_use)
        {
            s_pending[i].in_use = true;
            slot = (int)i;
            break;
        }
    }

    (void)xSemaphoreGive(s_pending_mutex);

    if (slot < 0)
    {
        return ATS_EC_NO_MEMORY;
    }

    s_pending[slot].service           = service;
    s_pending[slot].command           = command;
    s_pending[slot].response_buf      = response_payload;
    s_pending[slot].response_buf_size = *response_length;
    s_pending[slot].response_len      = response_length;
    s_pending[slot].result            = ATS_EC_TIMEOUT;
    s_pending[slot].request_id        = rid;

    status = send_frame(ATS_RPC_FRAME_TYPE_REQUEST, service, command, rid, request_payload, request_length);

    if (status != ATS_EC_OK)
    {
        (void)xSemaphoreTake(s_pending_mutex, portMAX_DELAY);
        s_pending[slot].in_use = false;
        (void)xSemaphoreGive(s_pending_mutex);
        return status;
    }

    wait_ticks = (timeout_ms == 0U) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_pending[slot].semaphore, wait_ticks) != pdTRUE)
    {
        (void)xSemaphoreTake(s_pending_mutex, portMAX_DELAY);
        s_pending[slot].in_use = false;
        (void)xSemaphoreGive(s_pending_mutex);
        return ATS_EC_TIMEOUT;
    }

    (void)xSemaphoreTake(s_pending_mutex, portMAX_DELAY);
    status = s_pending[slot].result;
    s_pending[slot].in_use = false;
    (void)xSemaphoreGive(s_pending_mutex);

    return status;
}

void ats_rpc_init(void)
{
    static const char *task_names[] = {"RPC_CTRL", "RPC_DISP", "RPC_DATA", "RPC_LOG"};
    UBaseType_t i;

    if (s_rpc_initialized)
    {
        return;
    }

    /* register callbacks */
    ats_net_rpc_register_callback(&s_net_rpc_callback);
    ats_printer_rpc_register_callback(&s_printer_rpc_callback);

    /* create pending table mutex */
    s_pending_mutex = xSemaphoreCreateMutex();

    /* create per-channel TX mutexes */
    for (i = 0U; i < ATS_RPC_CHANNEL_COUNT; ++i)
    {
        s_tx_mutex[i] = xSemaphoreCreateMutex();
    }

    /* create pending slot semaphores */
    for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
    {
        s_pending[i].semaphore = xSemaphoreCreateBinary();
        s_pending[i].in_use    = false;
        s_pending[i].request_id = 0U;
        if (s_pending[i].semaphore != NULL)
        {
            (void)xSemaphoreTake(s_pending[i].semaphore, 0U);
        }
    }

    /* clear handler table */
    for (i = 0U; i < ATS_RPC_MAX_SERVICES; ++i)
    {
        s_handlers[i] = NULL;
    }

    /* register service handlers */
    (void)ats_rpc_register_service(ATS_RPC_SERVICE_CORE, ats_rpc_core_handler);
    (void)ats_rpc_register_service(ATS_RPC_SERVICE_PRINTER, ats_rpc_printer_handler);
    (void)ats_rpc_register_service(ATS_RPC_SERVICE_NET, ats_rpc_net_handler);

    /* create 4 rpc tasks, one per channel */
    for (i = 0U; i < ATS_RPC_CHANNEL_COUNT; ++i)
    {
        if (xTaskCreate(rpc_task,
                        task_names[i],
                        1024U / sizeof(StackType_t),
                        (void *)(uintptr_t)i,
                        tskIDLE_PRIORITY + 2U,
                        NULL) != pdPASS)
        {
            for (;;)
            {
                __asm volatile ("wfi\n");
            }
        }
    }

    s_rpc_initialized = true;
}

void ats_rpc_register_service(uint8_t service, const ats_rpc_handler_t handler)
{
    if ((service == 0 || service >= ATS_RPC_MAX_SERVICES) || (handler == NULL))
        return;

    s_handlers[service] = handler;
}

void ats_rpc_dispatch(const ats_rpc_frame_t *frame)
{
    UBaseType_t i;

    if (frame == NULL || frame->service >= ATS_RPC_MAX_SERVICES)
    {
        return;
    }

    if (!s_rpc_initialized)
    {
        return;
    }

    if (frame->frame_type == ATS_RPC_FRAME_TYPE_RESPONSE)
    {
        uint8_t resp_request_id = frame->request_id;

        (void)xSemaphoreTake(s_pending_mutex, portMAX_DELAY);

        for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
        {
            if (!s_pending[i].in_use)
                continue;

            /* 按 request_id 精确匹配 */
            if (s_pending[i].request_id != resp_request_id)
                continue;

            if (frame->payload_length > s_pending[i].response_buf_size)
            {
                s_pending[i].result = ATS_EC_TOO_LARGE;
            }
            else
            {
                if (frame->payload_length != 0U)
                {
                    (void)memcpy(s_pending[i].response_buf, frame->payload, frame->payload_length);
                }

                *(s_pending[i].response_len) = frame->payload_length;
                s_pending[i].result = ATS_EC_OK;
            }

            (void)xSemaphoreGive(s_pending[i].semaphore);

            (void)xSemaphoreGive(s_pending_mutex);
            return;
        }

        (void)xSemaphoreGive(s_pending_mutex);
        return;
    }

    if ((frame->frame_type == ATS_RPC_FRAME_TYPE_REQUEST) || (frame->frame_type == ATS_RPC_FRAME_TYPE_EVENT))
    {
        ats_rpc_handler_t handler = s_handlers[frame->service];

        if (handler != NULL)
            handler(frame);
    }
}

int ats_rpc_core_handler(const ats_rpc_frame_t *frame)
{
    switch (frame->command)
    {
        case ATS_RPC_CORE_GET_THREAD_INFO:
        {
            char *buf = NULL;

            buf = (char *)ats_malloc(1024U);
            if (buf == NULL)
            {
                break;
            }

            if (ats_thread_info(buf, 1024U) != 0)
            {
                ats_free(buf);
                break;
            }

            /* Append heap info in same CSV format: name,remaining,total */
            {
                uint32_t used = 0U;
                uint32_t total = 0U;
                int heap_ret = ats_heap_info(&used, &total);
                if (heap_ret == 0)
                {
                    uint32_t remaining = total - used;
                    size_t existing_len = strlen(buf);
                    size_t remaining_buf = 1024U - existing_len;
                    if (remaining_buf > 32U)
                    {
                        snprintf(buf + existing_len, remaining_buf, "HEAP,%lu,%lu\n", (unsigned long)remaining, (unsigned long)total);
                    }
                }
            }

            ats_rpc_response(ATS_RPC_SERVICE_CORE, ATS_RPC_CORE_GET_THREAD_INFO, frame->request_id, (const uint8_t *)buf, (uint16_t)strlen(buf));
            ats_free(buf);
        } break;

        default:
            break;
    }

    return ATS_EC_OK;
}

int ats_rpc_printer_handler(const ats_rpc_frame_t *frame)
{
    switch (frame->command)
    {
        case ATS_RPC_PRINTER_CMD_SET_PAPER_STATUS:
        {
            bool paper_status = false;

            if (frame->payload_length >= 1U)
                paper_status = frame->payload[0] ? true : false;

            if (s_printer_rpc_callback.paper_status_change)
                s_printer_rpc_callback.paper_status_change(paper_status);
        } break;

        default:
            break;
    }

    return ATS_EC_OK;
}

int ats_rpc_net_handler(const ats_rpc_frame_t *frame)
{
    switch (frame->command)
    {
        case ATS_RPC_NET_CMD_MODE_CHANGE:
        {
            ats_net_mode_t mode = ATS_NET_MODE_CELLUALR;

            if (frame->payload_length >= 1U)
                mode = (ats_net_mode_t)ats_rpc_read_u32_le(frame->payload);

            if (s_net_rpc_callback.net_mode_change)
                s_net_rpc_callback.net_mode_change(mode);
        } break;

        case ATS_RPC_NET_CMD_STATUS_CHANGE:
        {
            bool status = false;

            if (frame->payload_length >= 1U)
                status = frame->payload[0] ? true : false;

            if (s_net_rpc_callback.net_status_change)
                s_net_rpc_callback.net_status_change(status);
        } break;

        case ATS_RPC_NET_CMD_WIFI_MODULE_STATUS_CHANGE:
        {
            bool module_status = false;

            if (frame->payload_length >= 1U)
                module_status = frame->payload[0] ? true : false;

            if (s_net_rpc_callback.wifi_module_status_change)
                s_net_rpc_callback.wifi_module_status_change(module_status);
        } break;

        default:
            break;
    }

    return ATS_EC_OK;
}
