/**
 * @file ats_lcd.c
 * @brief ARM/QEMU 侧 LCD RPC 客户端
 *
 * ARM 固件不再本地持有 Qt 可见的 framebuffer，而是把 LCD 操作编码成
 * RPC 事件发送给 ATS 宿主；ATS 侧再更新自己的 framebuffer 并渲染到界面。
 */

#include "ats_lcd.h"
#include "ats_error.h"
#include "ats_rpc.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATS_LCD_RECT_PAYLOAD_SIZE            10U
#define ATS_LCD_BITMAP1_HEADER_SIZE          14U
#define ATS_LCD_BITMAP16_HEADER_SIZE         9U

static unsigned short s_width = 0;
static unsigned short s_height = 0;

static void write_u16_le(uint8_t *buffer, unsigned short value);

/**
 * @brief 计算字节流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始字节数
 *
 * @return 最坏情况下的编码长度
 */
static uint16_t rle8_worst_case_size(uint16_t source_length)
{
    /* Worst case: alternating rep2-lit1 pattern.
     * Each rep2 block: 2 bytes (1 ctl + 1 data) for 2 source bytes (zero overhead).
     * Each lit1 block: 2 bytes (1 ctl + 1 data) for 1 source byte (+1 overhead).
     * Max overhead = (source_length + 2) / 3  =>  worst = source_length + (source_length + 2) / 3
     */
    return (uint16_t)(source_length + (source_length + 2U) / 3U);
}

/**
 * @brief 计算 16 位像素流 RLE 编码后的最坏情况长度
 *
 * @param[in] pixel_count 原始像素数
 *
 * @return 最坏情况下的编码长度（字节）
 */
static uint16_t rle16_worst_case_size(uint16_t pixel_count)
{
    /* Same worst-case block distribution as RLE8, but each pixel is 2 bytes.
     * Raw bytes = pixel_count * 2, max overhead same: (pixel_count + 2) / 3
     */
    return (uint16_t)((pixel_count * 2U) + (pixel_count + 2U) / 3U);
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
static uint16_t count_repeated_bytes(const uint8_t *source, uint16_t offset, uint16_t source_length)
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
static uint16_t encode_rle8(const uint8_t *source, uint16_t source_length,
                            uint8_t *destination, uint16_t destination_capacity)
{
    uint16_t source_offset = 0U;
    uint16_t destination_offset = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while (source_offset < source_length)
    {
        const uint16_t repeat_count = count_repeated_bytes(source, source_offset, source_length);
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
                if (count_repeated_bytes(source, source_offset, source_length) >= 2U)
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
 * @brief 统计连续重复 16 位像素数量
 *
 * @param[in] source 原始像素流
 * @param[in] offset 起始偏移
 * @param[in] pixel_count 原始像素总数
 *
 * @return 连续重复数量，范围 1..128
 */
static uint16_t count_repeated_words(const uint16_t *source, uint16_t offset, uint16_t pixel_count)
{
    uint16_t repeat_count = 1U;

    while (((uint16_t)(offset + repeat_count) < pixel_count) &&
           (repeat_count < 128U) &&
           (source[offset + repeat_count] == source[offset]))
    {
        ++repeat_count;
    }

    return repeat_count;
}

/**
 * @brief 对 16 位像素流执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块，长度 = control + 1，后跟对应数量的 RGB565 像素
 * - control bit7=1: 重复块，长度 = (control & 0x7F) + 1，后跟 1 个重复 RGB565 像素
 *
 * @param[in] source 原始像素流
 * @param[in] pixel_count 原始像素数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 编码后字节数，失败返回 0
 */
static uint16_t encode_rle16(const uint16_t *source, uint16_t pixel_count,
                             uint8_t *destination, uint16_t destination_capacity)
{
    uint16_t source_offset = 0U;
    uint16_t destination_offset = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while (source_offset < pixel_count)
    {
        const uint16_t repeat_count = count_repeated_words(source, source_offset, pixel_count);
        if (repeat_count >= 2U)
        {
            if ((uint16_t)(destination_offset + 3U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(0x80U | (uint8_t)(repeat_count - 1U));
            write_u16_le(&destination[destination_offset], source[source_offset]);
            destination_offset = (uint16_t)(destination_offset + 2U);
            source_offset = (uint16_t)(source_offset + repeat_count);
            continue;
        }

        {
            const uint16_t literal_start = source_offset;
            uint16_t literal_count = 1U;

            ++source_offset;
            while ((source_offset < pixel_count) && (literal_count < 128U))
            {
                if (count_repeated_words(source, source_offset, pixel_count) >= 2U)
                {
                    break;
                }

                ++source_offset;
                ++literal_count;
            }

            if ((uint16_t)(destination_offset + 1U + literal_count * 2U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(literal_count - 1U);
            {
                uint16_t literal_index;
                for (literal_index = 0U; literal_index < literal_count; ++literal_index)
                {
                    write_u16_le(&destination[destination_offset],
                                 source[(uint16_t)(literal_start + literal_index)]);
                    destination_offset = (uint16_t)(destination_offset + 2U);
                }
            }
        }
    }

    return destination_offset;
}

static void write_u16_le(uint8_t *buffer, unsigned short value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static int send_simple_lcd_event(uint8_t command, const uint8_t *payload, uint16_t payload_length)
{
    return ats_rpc_event(ATS_RPC_SERVICE_LCD,
                              command,
                              payload,
                              payload_length);
}

static int send_rect_command(uint8_t command,
                             unsigned short x, unsigned short y,
                             unsigned short width, unsigned short height,
                             unsigned short color)
{
    uint8_t payload[ATS_LCD_RECT_PAYLOAD_SIZE];

    write_u16_le(&payload[0], x);
    write_u16_le(&payload[2], y);
    write_u16_le(&payload[4], width);
    write_u16_le(&payload[6], height);
    write_u16_le(&payload[8], color);
    return send_simple_lcd_event(command, payload, sizeof(payload));
}

int ats_lcd_init(unsigned short width, unsigned short height)
{
    uint8_t payload[4];

    if ((width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    write_u16_le(&payload[0], width);
    write_u16_le(&payload[2], height);

    s_width = width;
    s_height = height;
    return send_simple_lcd_event(ATS_RPC_LCD_CMD_INIT, payload, sizeof(payload));
}

int ats_lcd_draw_rectangle(unsigned short x, unsigned short y,
                           unsigned short width, unsigned short height,
                           unsigned short rgbColor)
{
    if ((width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return send_rect_command(ATS_RPC_LCD_CMD_DRAW_RECTANGLE, x, y, width, height, rgbColor);
}

int ats_lcd_fill_rectangle(unsigned short x, unsigned short y,
                           unsigned short width, unsigned short height,
                           unsigned short rgbColor)
{
    if ((width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    return send_rect_command(ATS_RPC_LCD_CMD_FILL_RECTANGLE, x, y, width, height, rgbColor);
}

int ats_lcd_draw_1bit_bitmap(unsigned short x, unsigned short y,
                             unsigned short width, unsigned short height,
                             unsigned char *bitMapData,
                             unsigned short foregroundColor,
                             unsigned short backgroundColor,
                             bool isTransparent)
{
    const unsigned short bytes_per_row = (unsigned short)((width + 7U) / 8U);
    const uint16_t raw_bytes = (uint16_t)(bytes_per_row * height);
    const uint16_t worst_case = rle8_worst_case_size(raw_bytes);
    const uint16_t buffer_size = (uint16_t)(ATS_LCD_BITMAP1_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((bitMapData == NULL) || (width == 0U) || (height == 0U) || (bytes_per_row == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload = (uint8_t *)pvPortMalloc(buffer_size);
    if (payload == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    write_u16_le(&payload[0], x);
    write_u16_le(&payload[2], y);
    write_u16_le(&payload[4], width);
    write_u16_le(&payload[6], height);
    write_u16_le(&payload[8], foregroundColor);
    write_u16_le(&payload[10], backgroundColor);
    payload[12] = isTransparent ? 1U : 0U;
    payload[13] = ATS_RPC_BITMAP_ENCODING_RLE8;

    encoded_bytes = encode_rle8(bitMapData, raw_bytes,
                                &payload[ATS_LCD_BITMAP1_HEADER_SIZE], worst_case);
    if (encoded_bytes == 0U)
    {
        vPortFree(payload);
        return ATS_RPC_EC_TOO_LARGE;
    }

    status = send_simple_lcd_event(ATS_RPC_LCD_CMD_DRAW_1BIT_BITMAP,
                                   payload,
                                   (uint16_t)(ATS_LCD_BITMAP1_HEADER_SIZE + encoded_bytes));
    vPortFree(payload);
    return status;
}

int ats_lcd_draw_16bit_bitmap(unsigned short x, unsigned short y,
                              unsigned short width, unsigned short height,
                              unsigned short *bitMapData)
{
    const uint16_t pixel_count = (uint16_t)(width * height);
    const uint16_t worst_case = rle16_worst_case_size(pixel_count);
    const uint16_t buffer_size = (uint16_t)(ATS_LCD_BITMAP16_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((bitMapData == NULL) || (width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload = (uint8_t *)pvPortMalloc(buffer_size);
    if (payload == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    write_u16_le(&payload[0], x);
    write_u16_le(&payload[2], y);
    write_u16_le(&payload[4], width);
    write_u16_le(&payload[6], height);
    payload[8] = ATS_RPC_BITMAP_ENCODING_RLE16;

    encoded_bytes = encode_rle16(bitMapData, pixel_count,
                                 &payload[ATS_LCD_BITMAP16_HEADER_SIZE], worst_case);
    if (encoded_bytes == 0U)
    {
        vPortFree(payload);
        return ATS_RPC_EC_TOO_LARGE;
    }

    status = send_simple_lcd_event(ATS_RPC_LCD_CMD_DRAW_16BIT_BITMAP,
                                   payload,
                                   (uint16_t)(ATS_LCD_BITMAP16_HEADER_SIZE + encoded_bytes));
    vPortFree(payload);
    return status;
}

const unsigned short *ats_lcd_get_framebuffer(void)
{
    return NULL;
}

unsigned short ats_lcd_get_width(void)
{
    return s_width;
}

unsigned short ats_lcd_get_height(void)
{
    return s_height;
}

void ats_lcd_deinit(void)
{
    s_width = 0U;
    s_height = 0U;
    (void)send_simple_lcd_event(ATS_RPC_LCD_CMD_DEINIT, NULL, 0U);
}
