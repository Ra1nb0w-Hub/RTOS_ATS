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
#include "ats_rle.h"
#include "ats_sys.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATS_PRINTER_TEXT_HEADER_SIZE          3U
#define ATS_PRINTER_BITMAP_HEADER_SIZE        6U

static ats_printer_align_mode_t s_align_mode = ATS_PRINTER_ALIGN_MODE_LEFT;
static ats_printer_font_size_t s_font_size = ATS_PRINTER_FONT_SIZE_NORMAL;
static bool s_paper_status = true;

static void _paper_status_change_callback(bool status)
{
    s_paper_status = status;
}

int ats_printer_rpc_register_callback(ats_printer_rpc_callback_t *callback)
{
    if (!callback)
        return ATS_EC_INVALID_PARAM;

    callback->paper_status_change = _paper_status_change_callback;

    return ATS_EC_OK;
}

int ats_printer_open(void)
{
    s_align_mode = ATS_PRINTER_ALIGN_MODE_LEFT;
    s_font_size = ATS_PRINTER_FONT_SIZE_NORMAL;
    return ats_rpc_event(ATS_RPC_SERVICE_PRINTER, ATS_RPC_PRINTER_CMD_OPEN, NULL, 0U);
}

int ats_printer_close(void)
{
    return ats_rpc_event(ATS_RPC_SERVICE_PRINTER, ATS_RPC_PRINTER_CMD_CLOSE, NULL, 0U);
}

int ats_printer_start(void)
{
    return ats_rpc_event(ATS_RPC_SERVICE_PRINTER, ATS_RPC_PRINTER_CMD_START, NULL, 0U);
}

int ats_printer_set_align_mode(ats_printer_align_mode_t align)
{
    if (align > ATS_PRINTER_ALIGN_MODE_RIGHT)
        return ATS_EC_INVALID_PARAM;

    s_align_mode = align;
    return ATS_EC_OK;
}

int ats_printer_set_font_size(ats_printer_font_size_t size)
{
    s_font_size = size;
    return ATS_EC_OK;
}

int ats_printer_set_print_data(char *data, bool is_end_of_line)
{
    size_t text_length;
    uint16_t payload_size;
    uint8_t *payload;
    int status;

    if (data == NULL)
        return ATS_EC_INVALID_PARAM;

    text_length = strlen(data);
    payload_size = (uint16_t)(ATS_PRINTER_TEXT_HEADER_SIZE + text_length);
    payload = (uint8_t *)ats_malloc(payload_size);
    if (payload == NULL)
        return ATS_EC_NO_MEMORY;

    payload[0] = is_end_of_line ? 1U : 0U;
    payload[1] = (uint8_t)s_align_mode;
    payload[2] = (uint8_t)s_font_size;
    if (text_length != 0U)
    {
        (void)memcpy(&payload[ATS_PRINTER_TEXT_HEADER_SIZE], data, text_length);
    }

    status = ats_rpc_event(ATS_RPC_SERVICE_PRINTER,
                                ATS_RPC_PRINTER_CMD_PRINT_TEXT,
                                payload,
                                payload_size);
    ats_free(payload);
    return status;
}

int ats_printer_set_print_bitmap(unsigned char *data, int width, int height)
{
    const uint16_t bitmap_width = (uint16_t)width;
    const uint16_t bitmap_height = (uint16_t)height;
    const uint16_t bytes_per_row = (uint16_t)((bitmap_width + 7U) / 8U);
    const uint16_t raw_bytes = (uint16_t)(bytes_per_row * bitmap_height);
    const uint16_t worst_case = ats_rle8_worst_case_size(raw_bytes);
    const uint16_t buffer_size = (uint16_t)(ATS_PRINTER_BITMAP_HEADER_SIZE + worst_case);
    uint8_t *payload;
    uint16_t encoded_bytes;
    int status;

    if ((data == NULL) || (width <= 0) || (height <= 0) || (bytes_per_row == 0U))
        return ATS_EC_INVALID_PARAM;

    payload = (uint8_t *)ats_malloc(buffer_size);
    if (payload == NULL)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u16_le(&payload[0], bitmap_width);
    ats_rpc_write_u16_le(&payload[2], bitmap_height);
    payload[4] = (uint8_t)s_align_mode;
    payload[5] = ATS_RPC_BITMAP_ENCODING_RLE8;
    encoded_bytes = ats_rle8_encode(data, raw_bytes, &payload[ATS_PRINTER_BITMAP_HEADER_SIZE], worst_case);
    if (encoded_bytes == 0U)
    {
        ats_free(payload);
        return ATS_EC_TOO_LARGE;
    }

    status = ats_rpc_event(ATS_RPC_SERVICE_PRINTER,
                                ATS_RPC_PRINTER_CMD_PRINT_BITMAP,
                                payload,
                                (uint16_t)(ATS_PRINTER_BITMAP_HEADER_SIZE + encoded_bytes));
    ats_free(payload);
    return status;
}

bool ats_printer_get_paper_status(void)
{
    return s_paper_status;
}
