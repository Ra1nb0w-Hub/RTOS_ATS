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
    ATS_RPC_SERVICE_READER = 7,

    ATS_RPC_MAX_SERVICES
} ats_rpc_service_t;

typedef enum
{
    ATS_RPC_CORE_WRITE_LOG = 1,
    ATS_RPC_CORE_CRASH = 2,
    ATS_RPC_CORE_GET_TIMESTAMP = 3,
    ATS_RPC_CORE_GET_SERIAL_NUMBER = 4,
    ATS_RPC_CORE_GET_THREAD_INFO = 5
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
    ATS_RPC_PRINTER_CMD_START = 3,
    ATS_RPC_PRINTER_CMD_PRINT_TEXT = 4,
    ATS_RPC_PRINTER_CMD_PRINT_BITMAP = 5,
    ATS_RPC_PRINTER_CMD_SET_PAPER_STATUS = 6
} ats_rpc_printer_command_t;

typedef enum
{
    ATS_RPC_FS_CMD_OPEN = 1,
    ATS_RPC_FS_CMD_CLOSE = 2,
    ATS_RPC_FS_CMD_READ = 3,
    ATS_RPC_FS_CMD_WRITE = 4,
    ATS_RPC_FS_CMD_SEEK = 5,
    ATS_RPC_FS_CMD_SIZE = 6,
    ATS_RPC_FS_CMD_REMOVE = 7,
    ATS_RPC_FS_CMD_EXIST = 8
} ats_rpc_fs_command_t;

typedef enum
{
    ATS_RPC_NET_CMD_SOCK_CREATE = 1,
    ATS_RPC_NET_CMD_SOCK_CONNECT = 2,
    ATS_RPC_NET_CMD_SOCK_SEND = 3,
    ATS_RPC_NET_CMD_SOCK_RECV = 4,
    ATS_RPC_NET_CMD_SOCK_CLOSE = 5,
    ATS_RPC_NET_CMD_SET_MODE = 6,
    ATS_RPC_NET_CMD_STATUS_CHANGE = 7,
    ATS_RPC_NET_CMD_WIFI_MODULE_STATUS_CHANGE = 8,
    ATS_RPC_NET_CMD_WIFI_GET_SSID = 9,
    ATS_RPC_NET_CMD_WIFI_GET_SIGNAL = 10,
    ATS_RPC_NET_CMD_WIFI_GET_AP_LIST = 11,
    ATS_RPC_NET_CMD_CELLULAR_GET_MCC = 12,
    ATS_RPC_NET_CMD_CELLULAR_GET_MNC = 13,
    ATS_RPC_NET_CMD_CELLULAR_GET_LAC = 14,
    ATS_RPC_NET_CMD_CELLULAR_GET_CELL_ID = 15,
    ATS_RPC_NET_CMD_CELLULAR_GET_SIGNAL = 16,
    ATS_RPC_NET_CMD_CELLULAR_GET_IMSI = 17,
    ATS_RPC_NET_CMD_CELLULAR_GET_IMEI = 18
} ats_rpc_net_command_t;

typedef enum
{
    ATS_RPC_AUDIO_CMD_SET_VOLUME = 1,
    ATS_RPC_AUDIO_CMD_GET_VOLUME = 2,
    ATS_RPC_AUDIO_CMD_PLAY_FILE = 3
} ats_rpc_audio_command_t;

typedef enum
{
    ATS_RPC_READER_CMD_INIT = 1,
    ATS_RPC_READER_CMD_OPEN = 2,
    ATS_RPC_READER_CMD_CLOSE = 3,
    ATS_RPC_READER_CMD_POLL = 4,
    ATS_RPC_READER_CMD_CANCEL = 5,
    ATS_RPC_READER_CMD_ICC_POWER_ON = 6,
    ATS_RPC_READER_CMD_ICC_POWER_OFF = 7,
    ATS_RPC_READER_CMD_ICC_TRANSCEIVE_APDU = 8,
    ATS_RPC_READER_CMD_PICC_ACTIVATE = 9,
    ATS_RPC_READER_CMD_PICC_DEACTIVATE = 10,
    ATS_RPC_READER_CMD_PICC_TRANSCEIVE_APDU = 11,
    ATS_RPC_READER_CMD_GET_LAST_HW_ERROR = 12
} ats_rpc_reader_command_t;

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

void ats_rpc_write_u16_le(uint8_t *buffer, uint16_t value);
uint16_t ats_rpc_read_u16_le(const uint8_t *buffer);
void ats_rpc_write_u32_le(uint8_t *buffer, uint32_t value);
uint32_t ats_rpc_read_u32_le(const uint8_t *buffer);

int ats_rpc_event(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length);
void ats_rpc_event_for_crash(uint32_t pc, uint32_t lr);
int ats_rpc_response(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length);
int ats_rpc_request(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms);
int ats_rpc_request_ex(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms, uint32_t match_key);

void ats_rpc_init(void);
void ats_rpc_register_service(uint8_t service, const ats_rpc_handler_t handler);
void ats_rpc_dispatch(const ats_rpc_frame_t *frame);

int ats_rpc_core_handler(const ats_rpc_frame_t *frame);

#ifdef __cplusplus
}
#endif
