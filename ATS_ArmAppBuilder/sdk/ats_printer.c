/**
 * @file ats_printer.c
 * @brief ARM/QEMU 侧 Printer RPC 客户端
 *
 * ARM 固件不再本地渲染小票，而是把打印指令编码为 PRINTER 服务事件，
 * 由 ATS 宿主侧复用现有的小票渲染器进行预览展示。
 */

#include "ats_printer.h"
#include "ats_error.h"
#include "ats_rpc.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATS_PRINTER_TEXT_HEADER_SIZE          1U
#define ATS_PRINTER_BITMAP_HEADER_SIZE        5U

static ats_printer_align_mode_t s_align_mode = ATS_PRINTER_ALIGN_MODE_LEFT;
static ats_printer_font_size_t s_font_size = ATS_PRINTER_FONT_SIZE_NORMAL;
static bool s_paper_status = true;

/**
 * @brief 计算字节流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始字节数
 *
 * @return 最坏情况下的编码长度
 */
static uint16_t ats_printer_rle8_worst_case_size(uint16_t source_length)
{
    return (uint16_t)(source_length + ((source_length + 127U) / 128U));
}

/**
 * @brief 统计连续重复字节数量
 *
 * @param[in] source 原始字节流
 * @param[in] offset 起始偏移
 * @param[in] source_length 原始总长度
 *
 * @return 连续重复数量，范围 1..128
 */
static uint16_t ats_printer_count_repeated_bytes(const uint8_t *source,
                                                 uint16_t offset,
                                                 uint16_t source_length)
{
    uint16_t repeat_count = 1U;

    while (((uint16_t)(offset + repeat_count) < source_length) &&
           (repeat_count < 128U) &&
           (source[offset + repeat_count] == source[offset]))
    {
        ++repeat_count;
    }

    return repeat_count;
}

/**
 * @brief 对字节流执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块，长度 = control + 1，后跟原始字节
 * - control bit7=1: 重复块，长度 = (control & 0x7F) + 1，后跟 1 个重复字节
 *
 * @param[in] source 原始字节流
 * @param[in] source_length 原始字节数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 编码后字节数，失败返回 0
 */
static uint16_t ats_printer_encode_rle8(const uint8_t *source,
                                        uint16_t source_length,
                                        uint8_t *destination,
                                        uint16_t destination_capacity)
{
    uint16_t source_offset = 0U;
    uint16_t destination_offset = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while (source_offset < source_length)
    {
        const uint16_t repeat_count =
            ats_printer_count_repeated_bytes(source, source_offset, source_length);
        if (repeat_count >= 2U)
        {
            if ((uint16_t)(destination_offset + 2U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(0x80U | (uint8_t)(repeat_count - 1U));
            destination[destination_offset++] = source[source_offset];
            source_offset = (uint16_t)(source_offset + repeat_count);
            continue;
        }

        {
            const uint16_t literal_start = source_offset;
            uint16_t literal_count = 1U;

            ++source_offset;
            while ((source_offset < source_length) && (literal_count < 128U))
            {
                if (ats_printer_count_repeated_bytes(source, source_offset, source_length) >= 2U)
                {
                    break;
                }

                ++source_offset;
                ++literal_count;
            }

            if ((uint16_t)(destination_offset + 1U + literal_count) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(literal_count - 1U);
            (void)memcpy(&destination[destination_offset], &source[literal_start], literal_count);
            destination_offset = (uint16_t)(destination_offset + literal_count);
        }
    }

    return destination_offset;
}

/**
 * @brief 向缓冲区写入小端 16 位值
 *
 * @param[out] buffer 目标缓冲区
 * @param[in] value   写入值
 */
static void ats_printer_write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/**
 * @brief 发送无负载打印事件
 *
 * @param[in] command 命令号
 *
 * @return 0 表示成功，负值表示失败
 */
static int ats_printer_send_simple_event(uint8_t command)
{
    return ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER, command, NULL, 0U);
}

/**
 * @brief 查询宿主侧纸张状态
 *
 * @param[out] paper_status 输出纸张状态
 *
 * @return 0 表示成功，负值表示失败
 */
static int ats_printer_query_paper_status(bool *paper_status)
{
    uint8_t response_payload[1];
    uint16_t response_length = sizeof(response_payload);
    int status;

    if (paper_status == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    status = ats_rpc_request(ATS_RPC_SERVICE_PRINTER,
                             ATS_RPC_PRINTER_CMD_GET_PAPER_STATUS,
                             NULL,
                             0U,
                             response_payload,
                             &response_length,
                             100U);
    if ((status != ATS_EC_OK) || (response_length != 1U))
    {
        return (status != ATS_EC_OK) ? status : ATS_RPC_EC_FRAME;
    }

    *paper_status = (response_payload[0] != 0U);
    return ATS_EC_OK;
}

int ats_printer_open(void)
{
    s_align_mode = ATS_PRINTER_ALIGN_MODE_LEFT;
    s_font_size = ATS_PRINTER_FONT_SIZE_NORMAL;
    return ats_printer_send_simple_event(ATS_RPC_PRINTER_CMD_OPEN);
}

int ats_printer_close(void)
{
    return ats_printer_send_simple_event(ATS_RPC_PRINTER_CMD_CLOSE);
}

int ats_printer_set_align_mode(ats_printer_align_mode_t align)
{
    uint8_t payload[1];

    if (align > ATS_PRINTER_ALIGN_MODE_RIGHT)
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload[0] = (uint8_t)align;
    s_align_mode = align;
    return ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER,
                              ATS_RPC_PRINTER_CMD_SET_ALIGN,
                              payload,
                              sizeof(payload));
}

int ats_printer_get_align_mode(ats_printer_align_mode_t *align)
{
    if (align == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    *align = s_align_mode;
    return ATS_EC_OK;
}

int ats_printer_set_font_size(ats_printer_font_size_t size)
{
    uint8_t payload[1];

    if (size > ATS_PRINTER_FONT_SIZE_DOUBLE_WIDTH_HEIGHT)
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload[0] = (uint8_t)size;
    s_font_size = size;
    return ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER,
                              ATS_RPC_PRINTER_CMD_SET_FONT_SIZE,
                              payload,
                              sizeof(payload));
}

int ats_printer_get_font_size(ats_printer_font_size_t *size)
{
    if (size == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    *size = s_font_size;
    return ATS_EC_OK;
}

int ats_printer_set_print_data(char *data, bool is_end_of_line)
{
    size_t text_length;
    uint16_t payload_size;
    uint8_t *payload;
    int status;

    if (data == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    text_length = strlen(data);
    payload_size = (uint16_t)(ATS_PRINTER_TEXT_HEADER_SIZE + text_length);
    payload = (uint8_t *)pvPortMalloc(payload_size);
    if (payload == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    payload[0] = is_end_of_line ? 1U : 0U;
    if (text_length != 0U)
    {
        (void)memcpy(&payload[ATS_PRINTER_TEXT_HEADER_SIZE], data, text_length);
    }

    status = ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER,
                                ATS_RPC_PRINTER_CMD_PRINT_TEXT,
                                payload,
                                payload_size);
    vPortFree(payload);
    return status;
}

int ats_printer_set_print_bitmap(unsigned char *data, int width, int height)
{
    const uint16_t bitmap_width = (uint16_t)width;
    const uint16_t bitmap_height = (uint16_t)height;
    const uint16_t bytes_per_row = (uint16_t)((bitmap_width + 7U) / 8U);
    const uint16_t raw_bytes = (uint16_t)(bytes_per_row * bitmap_height);
    const uint16_t worst_case = ats_printer_rle8_worst_case_size(raw_bytes);
    const uint16_t buffer_size = (uint16_t)(ATS_PRINTER_BITMAP_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((data == NULL) || (width <= 0) || (height <= 0) || (bytes_per_row == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload = (uint8_t *)pvPortMalloc(buffer_size);
    if (payload == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    ats_printer_write_u16_le(&payload[0], bitmap_width);
    ats_printer_write_u16_le(&payload[2], bitmap_height);
    payload[4] = ATS_RPC_BITMAP_ENCODING_RLE8;
    encoded_bytes = ats_printer_encode_rle8(data, raw_bytes,
                                            &payload[ATS_PRINTER_BITMAP_HEADER_SIZE],
                                            worst_case);
    if (encoded_bytes == 0U)
    {
        vPortFree(payload);
        return ATS_RPC_EC_TOO_LARGE;
    }

    status = ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER,
                                ATS_RPC_PRINTER_CMD_PRINT_BITMAP,
                                payload,
                                (uint16_t)(ATS_PRINTER_BITMAP_HEADER_SIZE + encoded_bytes));
    vPortFree(payload);
    return status;
}

int ats_printer_set_paper_status(bool status)
{
    uint8_t payload[1];

    payload[0] = status ? 1U : 0U;
    s_paper_status = status;
    return ats_rpc_send_event(ATS_RPC_SERVICE_PRINTER,
                              ATS_RPC_PRINTER_CMD_SET_PAPER_STATUS,
                              payload,
                              sizeof(payload));
}

bool ats_printer_get_paper_status(void)
{
    bool remote_status = s_paper_status;

    if (ats_printer_query_paper_status(&remote_status) == ATS_EC_OK)
    {
        s_paper_status = remote_status;
    }

    return s_paper_status;
}

const unsigned char *ats_printer_get_receipt_buffer(int *width, int *height)
{
    (void)width;
    (void)height;
    return NULL;
}

void ats_printer_set_close_callback(ats_printer_close_callback_t callback)
{
    (void)callback;
}
