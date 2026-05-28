#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATS_RPC_SOF0                   0xA5U
#define ATS_RPC_SOF1                   0x5AU
#define ATS_RPC_HEADER_SIZE            7U

typedef enum
{
    ATS_RPC_FRAME_TYPE_REQUEST = 1,
    ATS_RPC_FRAME_TYPE_RESPONSE = 2,
    ATS_RPC_FRAME_TYPE_EVENT = 3
} ats_rpc_frame_type_t;

typedef enum
{
    ATS_RPC_SERVICE_CORE = 1,
    ATS_RPC_SERVICE_LCD = 2,
    ATS_RPC_SERVICE_PRINTER = 3,
    ATS_RPC_SERVICE_FS = 4,
    ATS_RPC_SERVICE_NET = 5,
    ATS_RPC_SERVICE_AUDIO = 6,

    ATS_RPC_MAX_SERVICES
} ats_rpc_service_t;

typedef enum
{
    ATS_RPC_CORE_WRITE_LOG = 1,
    ATS_RPC_CORE_CRASH = 2,
    ATS_RPC_CORE_SET_DATETIME = 3,
    ATS_RPC_CORE_GET_DATETIME = 4,
    ATS_RPC_CORE_GET_TIMESTAMP = 5,
    ATS_RPC_CORE_GET_SERIAL_NUMBER = 6,
    ATS_RPC_CORE_GET_THREAD_INFO = 7
} ats_rpc_core_command_t;

typedef enum
{
    ATS_RPC_LCD_CMD_INIT = 1,
    ATS_RPC_LCD_CMD_DRAW_RECTANGLE = 2,
    ATS_RPC_LCD_CMD_FILL_RECTANGLE = 3,
    ATS_RPC_LCD_CMD_DRAW_1BIT_BITMAP = 4,
    ATS_RPC_LCD_CMD_DRAW_16BIT_BITMAP = 5,
    ATS_RPC_LCD_CMD_DEINIT = 6
} ats_rpc_lcd_command_t;

typedef enum
{
    ATS_RPC_PRINTER_CMD_OPEN = 1,
    ATS_RPC_PRINTER_CMD_CLOSE = 2,
    ATS_RPC_PRINTER_CMD_SET_ALIGN = 3,
    ATS_RPC_PRINTER_CMD_SET_FONT_SIZE = 4,
    ATS_RPC_PRINTER_CMD_PRINT_TEXT = 5,
    ATS_RPC_PRINTER_CMD_PRINT_BITMAP = 6,
    ATS_RPC_PRINTER_CMD_SET_PAPER_STATUS = 7,
    ATS_RPC_PRINTER_CMD_GET_PAPER_STATUS = 8
} ats_rpc_printer_command_t;

typedef enum
{
    ATS_RPC_BITMAP_ENCODING_RAW = 0,
    ATS_RPC_BITMAP_ENCODING_RLE8 = 1,
    ATS_RPC_BITMAP_ENCODING_RLE16 = 2
} ats_rpc_bitmap_encoding_t;

typedef struct
{
    uint8_t frame_type;
    uint8_t service;
    uint8_t command;
    uint16_t payload_length;
    uint8_t *payload;
} ats_rpc_frame_t;

typedef int (*ats_rpc_handler_t)(const ats_rpc_frame_t *frame);

int ats_rpc_event(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length);
void ats_rpc_event_for_crash(uint32_t pc, uint32_t lr);
int ats_rpc_response(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length);
int ats_rpc_request(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms);

void ats_rpc_init(void);
void ats_rpc_register_service(uint8_t service, const ats_rpc_handler_t handler);
void ats_rpc_dispatch(const ats_rpc_frame_t *frame);

int ats_rpc_core_handler(const ats_rpc_frame_t *frame);

#ifdef __cplusplus
}
#endif
