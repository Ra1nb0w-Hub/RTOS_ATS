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
#include "ats_rle.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATS_LCD_RECT_PAYLOAD_SIZE            10U
#define ATS_LCD_BITMAP1_HEADER_SIZE          14U
#define ATS_LCD_BITMAP16_HEADER_SIZE         9U

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

    ats_rpc_write_u16_le(&payload[0], x);
    ats_rpc_write_u16_le(&payload[2], y);
    ats_rpc_write_u16_le(&payload[4], width);
    ats_rpc_write_u16_le(&payload[6], height);
    ats_rpc_write_u16_le(&payload[8], color);
    return send_simple_lcd_event(command, payload, sizeof(payload));
}

int ats_lcd_init(unsigned short width, unsigned short height)
{
    uint8_t payload[4];

    if ((width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    ats_rpc_write_u16_le(&payload[0], width);
    ats_rpc_write_u16_le(&payload[2], height);

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
    const uint16_t worst_case = ats_rle8_worst_case_size(raw_bytes);
    const uint16_t buffer_size = (uint16_t)(ATS_LCD_BITMAP1_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((bitMapData == NULL) || (width == 0U) || (height == 0U) || (bytes_per_row == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload = (uint8_t *)ats_malloc(buffer_size);
    if (payload == NULL)
    {
        return ATS_EC_NO_MEMORY;
    }

    ats_rpc_write_u16_le(&payload[0], x);
    ats_rpc_write_u16_le(&payload[2], y);
    ats_rpc_write_u16_le(&payload[4], width);
    ats_rpc_write_u16_le(&payload[6], height);
    ats_rpc_write_u16_le(&payload[8], foregroundColor);
    ats_rpc_write_u16_le(&payload[10], backgroundColor);
    payload[12] = isTransparent ? 1U : 0U;
    payload[13] = ATS_RPC_BITMAP_ENCODING_RLE8;

    encoded_bytes = ats_rle8_encode(bitMapData, raw_bytes, &payload[ATS_LCD_BITMAP1_HEADER_SIZE], worst_case);
    if (encoded_bytes == 0U)
    {
        ats_free(payload);
        return ATS_EC_TOO_LARGE;
    }

    status = send_simple_lcd_event(ATS_RPC_LCD_CMD_DRAW_1BIT_BITMAP,
                                   payload,
                                   (uint16_t)(ATS_LCD_BITMAP1_HEADER_SIZE + encoded_bytes));
    ats_free(payload);
    return status;
}

int ats_lcd_draw_16bit_bitmap(unsigned short x, unsigned short y,
                              unsigned short width, unsigned short height,
                              unsigned short *bitMapData)
{
    const uint16_t pixel_count = (uint16_t)(width * height);
    const uint16_t worst_case = ats_rle16_worst_case_size(pixel_count);
    const uint16_t buffer_size = (uint16_t)(ATS_LCD_BITMAP16_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((bitMapData == NULL) || (width == 0U) || (height == 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    payload = (uint8_t *)ats_malloc(buffer_size);
    if (payload == NULL)
    {
        return ATS_EC_NO_MEMORY;
    }

    ats_rpc_write_u16_le(&payload[0], x);
    ats_rpc_write_u16_le(&payload[2], y);
    ats_rpc_write_u16_le(&payload[4], width);
    ats_rpc_write_u16_le(&payload[6], height);
    payload[8] = ATS_RPC_BITMAP_ENCODING_RLE16;

    encoded_bytes = ats_rle16_encode(bitMapData, pixel_count, &payload[ATS_LCD_BITMAP16_HEADER_SIZE], worst_case);
    if (encoded_bytes == 0U)
    {
        ats_free(payload);
        return ATS_EC_TOO_LARGE;
    }

    status = send_simple_lcd_event(ATS_RPC_LCD_CMD_DRAW_16BIT_BITMAP,
                                   payload,
                                   (uint16_t)(ATS_LCD_BITMAP16_HEADER_SIZE + encoded_bytes));
    ats_free(payload);
    return status;
}

void ats_lcd_deinit(void)
{
    (void)send_simple_lcd_event(ATS_RPC_LCD_CMD_DEINIT, NULL, 0U);
}
