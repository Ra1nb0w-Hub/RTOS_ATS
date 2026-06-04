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

#define ATS_RPC_MAX_PENDING              4U

/* ------------------------------------------------------------------
 *  UART definitions
 * ------------------------------------------------------------------ */
#define ATS_UART_BASE              0x40004000UL
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
    uint32_t            match_key;
    SemaphoreHandle_t   semaphore;
    bool                in_use;
} ats_rpc_pending_t;

static ats_net_rpc_callback_t     s_net_rpc_callback = {0};
static ats_printer_rpc_callback_t s_printer_rpc_callback = {0};
static ats_rpc_pending_t          s_pending[ATS_RPC_MAX_PENDING];
static ats_rpc_handler_t          s_handlers[ATS_RPC_MAX_SERVICES];
static bool                       s_rpc_initialized = false;

/* ------------------------------------------------------------------
 *  UART low-level helpers
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

static void uart_init_tx(void)
{
    uint32_t ctrl = uart_read(ATS_UART_BASE, ATS_UART_CTRL_OFFSET);
    uart_write(ATS_UART_BASE, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, ctrl | ATS_UART_CTRL_TX_EN);
}

static void uart_init_rx(void)
{
    uint32_t ctrl = uart_read(ATS_UART_BASE, ATS_UART_CTRL_OFFSET);
    uart_write(ATS_UART_BASE, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, ctrl | ATS_UART_CTRL_TX_EN | ATS_UART_CTRL_RX_EN);
}

static bool uart_try_read_byte(uint8_t *byte)
{
    if (byte == NULL)
    {
        return false;
    }

    if ((uart_read(ATS_UART_BASE, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_RXFULL) == 0U)
    {
        return false;
    }

    *byte = (uint8_t)(uart_read(ATS_UART_BASE, ATS_UART_DATA_OFFSET) & 0xFFU);
    return true;
}

/* ------------------------------------------------------------------
 *  transport write — UART
 * ------------------------------------------------------------------ */
static int ats_rpc_transport_write(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    uint16_t batch;

    if ((data == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    uart_init_tx();

    index = 0U;
    while (index < length)
    {
        while (uart_read(ATS_UART_BASE, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_TXFULL)
        {
        }

        batch = 0U;
        while ((batch < ATS_UART_TX_FIFO_DEPTH) && (index < length))
        {
            uart_write(ATS_UART_BASE, ATS_UART_DATA_OFFSET, (uint32_t)data[index]);
            ++index;
            ++batch;
        }
    }

    return ATS_EC_OK;
}

/* ------------------------------------------------------------------
 *  transport read — UART
 * ------------------------------------------------------------------ */
static int ats_rpc_transport_read(uint8_t *byte, uint32_t timeout_ms)
{
    uint32_t     start_tick = 0U;
    BaseType_t   scheduler_state;

    if (byte == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    uart_init_rx();

    scheduler_state = xTaskGetSchedulerState();
    if (scheduler_state != taskSCHEDULER_NOT_STARTED)
    {
        start_tick = xTaskGetTickCount();
    }

    for (;;)
    {
        if (uart_try_read_byte(byte))
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

static int read_exact(uint8_t *buffer, uint16_t length, uint32_t timeout_ms)
{
    uint16_t index;

    if ((buffer == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    for (index = 0U; index < length; ++index)
    {
        const int status = ats_rpc_transport_read(&buffer[index], timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }
    }

    return ATS_EC_OK;
}

static int send_frame(uint8_t frame_type, uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    const uint16_t frame_length = (uint16_t)(ATS_RPC_HEADER_SIZE + payload_length);
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
    ats_rpc_write_u16_le(&frame_buffer[5], payload_length);

    if (payload_length != 0U)
    {
        (void)memcpy(&frame_buffer[ATS_RPC_HEADER_SIZE], payload, payload_length);
    }

    status = ats_rpc_transport_write(frame_buffer, frame_length);
    ats_free(frame_buffer);
    return status;
}

static int recv_frame(ats_rpc_frame_t *frame, uint32_t timeout_ms)
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
        status = read_exact(&header[0], 1U, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if (header[0] != ATS_RPC_SOF0)
        {
            continue;
        }

        status = read_exact(&header[1], 1U, timeout_ms);
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

    status = read_exact(&header[2], (uint16_t)(ATS_RPC_HEADER_SIZE - 2U), timeout_ms);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    payload_length = ats_rpc_read_u16_le(&header[5]);

    if (payload_length != 0U)
    {
        data_buffer = (uint8_t *)ats_malloc(payload_length);
        if (data_buffer == NULL)
        {
            return ATS_EC_NO_MEMORY;
        }

        status = read_exact(data_buffer, payload_length, timeout_ms);
        if (status != ATS_EC_OK)
        {
            ats_free(data_buffer);
            return status;
        }

        frame->payload = data_buffer;
    }
    else
    {
        data_buffer = NULL;
        frame->payload = NULL;
    }

    frame->frame_type = header[2];
    frame->service = header[3];
    frame->command = header[4];
    frame->payload_length = payload_length;

    return ATS_EC_OK;
}

static void rpc_task(void *args)
{
    (void)args;
    (void)ats_rpc_register_service(ATS_RPC_SERVICE_CORE, ats_rpc_core_handler);

    for (;;)
    {
        ats_rpc_frame_t frame;
        int status;

        (void)memset(&frame, 0, sizeof(frame));

        status = recv_frame(&frame, portMAX_DELAY);
        if (status != ATS_EC_OK)
        {
            continue;
        }

        ats_rpc_dispatch(&frame);

        if (frame.payload != NULL)
        {
            ats_free(frame.payload);
            frame.payload = NULL;
        }
    }
}

int ats_rpc_event(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_EVENT, service, command, payload, payload_length);
}

void ats_rpc_event_for_crash(uint32_t pc, uint32_t lr)
{
    uint8_t frame[31U] = {0};

    if (!s_rpc_initialized)
    {
        return;
    }

    frame[0] = ATS_RPC_SOF0;
    frame[1] = ATS_RPC_SOF1;
    frame[2] = ATS_RPC_FRAME_TYPE_EVENT;
    frame[3] = ATS_RPC_SERVICE_CORE;
    frame[4] = ATS_RPC_CORE_CRASH;
    frame[5] = 24U;
    frame[6] = 0U;

    /* PC [7..10] */
    frame[7]  = (uint8_t)(pc & 0xFFU);
    frame[8]  = (uint8_t)((pc >> 8) & 0xFFU);
    frame[9]  = (uint8_t)((pc >> 16) & 0xFFU);
    frame[10] = (uint8_t)((pc >> 24) & 0xFFU);

    /* LR [11..14] */
    frame[11] = (uint8_t)(lr & 0xFFU);
    frame[12] = (uint8_t)((lr >> 8) & 0xFFU);
    frame[13] = (uint8_t)((lr >> 16) & 0xFFU);
    frame[14] = (uint8_t)((lr >> 24) & 0xFFU);

    /* CFSR [15..18] — 0xE000ED28 */
    {
        const uint32_t cfsr = *(const volatile uint32_t *)0xE000ED28U;
        frame[15] = (uint8_t)(cfsr & 0xFFU);         /* MMFSR */
        frame[16] = (uint8_t)((cfsr >> 8) & 0xFFU);   /* BFSR */
        frame[17] = (uint8_t)((cfsr >> 16) & 0xFFU);  /* UFSR */
        frame[18] = 0U;
    }

    /* HFSR [19..22] — 0xE000ED2C */
    {
        const uint32_t hfsr = *(const volatile uint32_t *)0xE000ED2CU;
        frame[19] = (uint8_t)(hfsr & 0xFFU);
        frame[20] = (uint8_t)((hfsr >> 8) & 0xFFU);
        frame[21] = (uint8_t)((hfsr >> 16) & 0xFFU);
        frame[22] = (uint8_t)((hfsr >> 24) & 0xFFU);
    }

    /* BFAR [23..26] — 0xE000ED38 */
    {
        const uint32_t bfar = *(const volatile uint32_t *)0xE000ED38U;
        frame[23] = (uint8_t)(bfar & 0xFFU);
        frame[24] = (uint8_t)((bfar >> 8) & 0xFFU);
        frame[25] = (uint8_t)((bfar >> 16) & 0xFFU);
        frame[26] = (uint8_t)((bfar >> 24) & 0xFFU);
    }

    /* MMFAR [27..30] — 0xE000ED34 */
    {
        const uint32_t mmfar = *(const volatile uint32_t *)0xE000ED34U;
        frame[27] = (uint8_t)(mmfar & 0xFFU);
        frame[28] = (uint8_t)((mmfar >> 8) & 0xFFU);
        frame[29] = (uint8_t)((mmfar >> 16) & 0xFFU);
        frame[30] = (uint8_t)((mmfar >> 24) & 0xFFU);
    }

    (void)ats_rpc_transport_write(frame, sizeof(frame));
}

int ats_rpc_response(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_RESPONSE, service, command, payload, payload_length);
}

int ats_rpc_request(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms)
{
    return ats_rpc_request_ex(service, command, request_payload, request_length, response_payload, response_length, timeout_ms, 0U);
}

int ats_rpc_request_ex(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms, uint32_t match_key)
{
    int slot = -1;
    int status;
    TickType_t wait_ticks;
    UBaseType_t i;

    if ((response_length == NULL) || ((response_payload == NULL) && (*response_length != 0U)))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (!s_rpc_initialized)
    {
        return ATS_EC_RPC_NOT_INIT;
    }

    taskENTER_CRITICAL();
    for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
    {
        if (!s_pending[i].in_use)
        {
            s_pending[i].in_use = true;
            slot = (int)i;
            break;
        }
    }
    taskEXIT_CRITICAL();

    if (slot < 0)
    {
        return ATS_EC_NO_MEMORY;
    }

    s_pending[slot].service          = service;
    s_pending[slot].command          = command;
    s_pending[slot].response_buf     = response_payload;
    s_pending[slot].response_buf_size = *response_length;
    s_pending[slot].response_len     = response_length;
    s_pending[slot].result           = ATS_EC_TIMEOUT;
    s_pending[slot].match_key        = match_key;

    status = send_frame(ATS_RPC_FRAME_TYPE_REQUEST, service, command, request_payload, request_length);
    if (status != ATS_EC_OK)
    {
        s_pending[slot].in_use = false;
        return status;
    }

    wait_ticks = (timeout_ms == 0U) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_pending[slot].semaphore, wait_ticks) != pdTRUE)
    {
        s_pending[slot].in_use = false;
        return ATS_EC_TIMEOUT;
    }

    status = s_pending[slot].result;
    s_pending[slot].in_use = false;
    return status;
}

void ats_rpc_init(void)
{
    UBaseType_t i;

    if (s_rpc_initialized)
    {
        return;
    }

    ats_net_rpc_register_callback(&s_net_rpc_callback);
    ats_printer_rpc_register_callback(&s_printer_rpc_callback);

    for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
    {
        s_pending[i].semaphore = xSemaphoreCreateBinary();
        s_pending[i].in_use = false;
        if (s_pending[i].semaphore != NULL)
        {
            (void)xSemaphoreTake(s_pending[i].semaphore, 0U);
        }
    }

    for (int i = 0U; i < ATS_RPC_MAX_SERVICES; ++i)
    {
        s_handlers[i] = NULL;
    }

    if (xTaskCreate(rpc_task, "RPC", 2048U / sizeof(StackType_t), NULL, tskIDLE_PRIORITY + 2U, NULL) != pdPASS)
    {
        for (;;)
        {
            __asm volatile ("wfi\n");
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
        uint32_t resp_match_key = 0U;

        /* 提取响应 payload 前 4 字节作为 match_key (用于多 socket 场景的请求匹配) */
        if (frame->payload_length >= 4U)
            resp_match_key = ats_rpc_read_u32_le(frame->payload);

        for (i = 0U; i < ATS_RPC_MAX_PENDING; ++i)
        {
            if (s_pending[i].in_use &&
                (s_pending[i].service == frame->service) &&
                (s_pending[i].command == frame->command))
            {
                /* 如果待处理槽设置了 match_key，则必须与响应匹配 */
                if (s_pending[i].match_key != 0U && s_pending[i].match_key != resp_match_key)
                {
                    continue; /* 跳过，继续查找下一个匹配 */
                }

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
                return;
            }
        }

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

            ats_rpc_response(ATS_RPC_SERVICE_CORE, ATS_RPC_CORE_GET_THREAD_INFO, (const uint8_t *)buf, (uint16_t)strlen(buf));
            ats_free(buf);
        } break;

        default:
            break;
    }

    return ATS_EC_OK;
}
