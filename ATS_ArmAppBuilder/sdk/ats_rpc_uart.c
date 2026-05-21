#include "ats_rpc.h"
#include "ats_error.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef ATS_CPU_CLOCK_HZ
#define ATS_CPU_CLOCK_HZ               25000000UL
#endif

#define ATS_UART0_BASE                 0x40004000UL
#define ATS_UART1_BASE                 0x40005000UL
#define ATS_UART2_BASE                 0x40006000UL
#define ATS_UART_DATA_OFFSET           0x000UL
#define ATS_UART_STATE_OFFSET          0x004UL
#define ATS_UART_CTRL_OFFSET           0x008UL
#define ATS_UART_BAUDDIV_OFFSET        0x010UL

#define ATS_UART_STATE_TXFULL          (1UL << 0)
#define ATS_UART_STATE_RXFULL          (1UL << 1)

#define ATS_UART_CTRL_TX_EN            (1UL << 0)
#define ATS_UART_CTRL_RX_EN            (1UL << 1)

#define ATS_UART_BAUD_RATE             921600UL
#define ATS_UART_BAUD_DIVISOR          (ATS_CPU_CLOCK_HZ / ATS_UART_BAUD_RATE)

typedef struct
{
    uint32_t base;
    bool initialized;
} ats_uart_instance_t;

static ats_uart_instance_t s_uarts[] =
{
    { ATS_UART0_BASE, false },
    { ATS_UART1_BASE, false },
    { ATS_UART2_BASE, false }
};

enum
{
    ATS_UART_INDEX_LOG = 0,
    ATS_UART_INDEX_RPC = 1,
    ATS_UART_INDEX_LCD = 2
};

static volatile uint32_t *ats_uart_reg32(uint32_t address)
{
    return (volatile uint32_t *)address;
}

static uint32_t ats_uart_read_reg(uint32_t base, uint32_t offset)
{
    return *ats_uart_reg32(base + offset);
}

static void ats_uart_write_reg(uint32_t base, uint32_t offset, uint32_t value)
{
    *ats_uart_reg32(base + offset) = value;
}

static void ats_uart_init_once(ats_uart_instance_t *uart, bool enable_rx)
{
    uint32_t ctrl_value;

    if ((uart == NULL) || uart->initialized)
    {
        return;
    }

    ctrl_value = ATS_UART_CTRL_TX_EN;
    if (enable_rx)
    {
        ctrl_value |= ATS_UART_CTRL_RX_EN;
    }

    ats_uart_write_reg(uart->base, ATS_UART_CTRL_OFFSET, 0U);
    ats_uart_write_reg(uart->base, ATS_UART_BAUDDIV_OFFSET, ATS_UART_BAUD_DIVISOR);
    ats_uart_write_reg(uart->base, ATS_UART_CTRL_OFFSET, ctrl_value);

    uart->initialized = true;
}

static bool ats_uart_try_read_byte(ats_uart_instance_t *uart, uint8_t *byte)
{
    if ((uart == NULL) || (byte == NULL))
    {
        return false;
    }

    if ((ats_uart_read_reg(uart->base, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_RXFULL) == 0U)
    {
        return false;
    }

    *byte = (uint8_t)(ats_uart_read_reg(uart->base, ATS_UART_DATA_OFFSET) & 0xFFU);
    return true;
}

static void ats_uart_write_byte(ats_uart_instance_t *uart, uint8_t byte)
{
    if (uart == NULL)
    {
        return;
    }

    while ((ats_uart_read_reg(uart->base, ATS_UART_STATE_OFFSET) & ATS_UART_STATE_TXFULL) != 0U)
    {
    }

    ats_uart_write_reg(uart->base, ATS_UART_DATA_OFFSET, (uint32_t)byte);
}

static int ats_uart_transport_write(ats_uart_instance_t *uart, const uint8_t *data, uint16_t length)
{
    uint16_t index;

    if ((data == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    ats_uart_init_once(uart, false);

    for (index = 0U; index < length; ++index)
    {
        ats_uart_write_byte(uart, data[index]);
    }

    return ATS_EC_OK;
}

int ats_rpc_log_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)user_data;
    return ats_uart_transport_write(&s_uarts[ATS_UART_INDEX_LOG], data, length);
}

int ats_rpc_lcd_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)user_data;
    return ats_uart_transport_write(&s_uarts[ATS_UART_INDEX_LCD], data, length);
}

int ats_rpc_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)user_data;
    return ats_uart_transport_write(&s_uarts[ATS_UART_INDEX_RPC], data, length);
}

int ats_rpc_transport_read(uint8_t *byte, uint32_t timeout_ms, void *user_data)
{
    uint32_t start_tick = 0U;
    BaseType_t scheduler_state;

    (void)user_data;

    if (byte == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    ats_uart_init_once(&s_uarts[ATS_UART_INDEX_RPC], true);

    scheduler_state = xTaskGetSchedulerState();
    if (scheduler_state != taskSCHEDULER_NOT_STARTED)
    {
        start_tick = xTaskGetTickCount();
    }

    for (;;)
    {
        if (ats_uart_try_read_byte(&s_uarts[ATS_UART_INDEX_RPC], byte))
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
