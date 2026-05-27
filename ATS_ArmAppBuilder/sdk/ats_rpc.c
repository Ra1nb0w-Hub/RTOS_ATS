#include "ats_rpc.h"
#include "ats_error.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include "FreeRTOS/task.h"
#include <string.h>

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
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, 0U);
    uart_write(ATS_UART_BASE, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, ATS_UART_CTRL_TX_EN);
}

static void uart_init_rx(void)
{
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, 0U);
    uart_write(ATS_UART_BASE, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    uart_write(ATS_UART_BASE, ATS_UART_CTRL_OFFSET, ATS_UART_CTRL_TX_EN | ATS_UART_CTRL_RX_EN);
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
int ats_rpc_transport_write(const uint8_t *data, uint16_t length)
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
int ats_rpc_transport_read(uint8_t *byte, uint32_t timeout_ms)
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

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
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

static int send_frame(uint8_t frame_type,
                      uint8_t service, uint8_t command,
                      const uint8_t *payload, uint16_t payload_length)
{
    const uint16_t frame_length = (uint16_t)(ATS_RPC_HEADER_SIZE + payload_length);
    uint8_t *frame_buffer;
    int status;

    if ((payload == NULL) && (payload_length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    frame_buffer = (uint8_t *)pvPortMalloc(frame_length);
    if (frame_buffer == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    frame_buffer[0] = ATS_RPC_SOF0;
    frame_buffer[1] = ATS_RPC_SOF1;
    frame_buffer[2] = frame_type;
    frame_buffer[3] = service;
    frame_buffer[4] = command;
    write_u16_le(&frame_buffer[5], payload_length);

    if (payload_length != 0U)
    {
        (void)memcpy(&frame_buffer[ATS_RPC_HEADER_SIZE], payload, payload_length);
    }

    status = ats_rpc_transport_write(frame_buffer, frame_length);
    vPortFree(frame_buffer);
    return status;
}

int ats_rpc_send_request(uint8_t service, uint8_t command,
                         const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_REQUEST,
                      service, command,
                      payload, payload_length);
}

int ats_rpc_send_response(uint8_t service, uint8_t command,
                          const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_RESPONSE,
                      service, command,
                      payload, payload_length);
}

int ats_rpc_send_event(uint8_t service, uint8_t command,
                       const uint8_t *payload, uint16_t payload_length)
{
    return send_frame(ATS_RPC_FRAME_TYPE_EVENT,
                      service, command,
                      payload, payload_length);
}

int ats_rpc_receive(ats_rpc_frame_t *frame, uint32_t timeout_ms)
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

    payload_length = read_u16_le(&header[5]);

    if (payload_length != 0U)
    {
        data_buffer = (uint8_t *)pvPortMalloc(payload_length);
        if (data_buffer == NULL)
        {
            return ATS_RPC_EC_NO_MEMORY;
        }

        status = read_exact(data_buffer, payload_length, timeout_ms);
        if (status != ATS_EC_OK)
        {
            vPortFree(data_buffer);
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

int ats_rpc_request(uint8_t service, uint8_t command,
                    const uint8_t *request_payload, uint16_t request_length,
                    uint8_t *response_payload, uint16_t *response_length,
                    uint32_t timeout_ms)
{
    ats_rpc_frame_t frame;
    int status;

    if ((response_length == NULL) || ((response_payload == NULL) && (*response_length != 0U)))
    {
        return ATS_EC_INVALID_PARAM;
    }

    status = ats_rpc_send_request(service, command, request_payload, request_length);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    for (;;)
    {
        ats_rpc_frame_free(&frame);
        status = ats_rpc_receive(&frame, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if ((frame.frame_type != ATS_RPC_FRAME_TYPE_RESPONSE) ||
            (frame.service != service) ||
            (frame.command != command))
        {
            continue;
        }

        if (frame.payload_length > *response_length)
        {
            ats_rpc_frame_free(&frame);
            return ATS_RPC_EC_TOO_LARGE;
        }

        if (frame.payload_length != 0U)
        {
            (void)memcpy(response_payload, frame.payload, frame.payload_length);
        }

        *response_length = frame.payload_length;
        ats_rpc_frame_free(&frame);
        return ATS_EC_OK;
    }
}

void ats_rpc_frame_free(ats_rpc_frame_t *frame)
{
    if (frame == NULL || frame->payload == NULL)
    {
        return;
    }

    vPortFree(frame->payload);
    frame->payload = NULL;
    frame->payload_length = 0U;
}

int ats_rpc_log_event(const char *message)
{
    if (message == NULL || message[0] == '\0')
    {
        return ATS_EC_INVALID_PARAM;
    }

    return ats_rpc_send_event(ATS_RPC_SERVICE_CORE,
                              ATS_RPC_CORE_WRITE_LOG,
                              (const uint8_t *)message,
                              (uint16_t)strlen(message));
}
