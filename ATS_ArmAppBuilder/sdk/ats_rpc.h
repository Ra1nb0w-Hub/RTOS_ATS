#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATS_RPC_SOF0                   0xA5U
#define ATS_RPC_SOF1                   0x5AU
#define ATS_RPC_HEADER_SIZE            6U

#define ATS_RPC_EC_BAD_STATE           (-10)
#define ATS_RPC_EC_IO                  (-11)
#define ATS_RPC_EC_FRAME               (-13)
#define ATS_RPC_EC_TOO_LARGE           (-14)
#define ATS_RPC_EC_NO_MEMORY           (-15)

typedef enum
{
    ATS_RPC_FRAME_TYPE_REQUEST = 1,
    ATS_RPC_FRAME_TYPE_RESPONSE = 2,
    ATS_RPC_FRAME_TYPE_EVENT = 3
} ats_rpc_frame_type_t;

typedef enum
{
    ATS_RPC_SERVICE_CORE = 1,
    ATS_RPC_SERVICE_LOG = 2,
    ATS_RPC_SERVICE_LCD = 16,
    ATS_RPC_SERVICE_PRINTER = 17,
    ATS_RPC_SERVICE_FS = 18,
    ATS_RPC_SERVICE_NET = 19,
    ATS_RPC_SERVICE_AUDIO = 20
} ats_rpc_service_t;

typedef enum
{
    ATS_RPC_CORE_CMD_PING = 1,
    ATS_RPC_CORE_CMD_CAPABILITIES = 2
} ats_rpc_core_command_t;

typedef enum
{
    ATS_RPC_LOG_CMD_WRITE = 1
} ats_rpc_log_command_t;

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

typedef int (*ats_rpc_write_fn)(const uint8_t *data, uint16_t length, void *user_data);
typedef int (*ats_rpc_read_fn)(uint8_t *byte, uint32_t timeout_ms, void *user_data);

typedef struct
{
    ats_rpc_write_fn write;
    ats_rpc_read_fn read;
    void *user_data;
} ats_rpc_transport_t;

typedef struct
{
    uint8_t frame_type;
    uint8_t flags;
    uint8_t service;
    uint8_t command;
    uint16_t payload_length;
    uint8_t *payload;
} ats_rpc_frame_t;

int ats_rpc_init(const ats_rpc_transport_t *transport);
int ats_rpc_init_default(void);
void ats_rpc_deinit(void);
bool ats_rpc_is_initialized(void);

int ats_rpc_send_request(uint8_t service, uint8_t command,
                         const uint8_t *payload, uint16_t payload_length,
                         uint8_t flags);
int ats_rpc_send_response(uint8_t service, uint8_t command,
                          const uint8_t *payload, uint16_t payload_length,
                          uint8_t flags);
int ats_rpc_send_event(uint8_t service, uint8_t command,
                       const uint8_t *payload, uint16_t payload_length,
                       uint8_t flags);
int ats_rpc_send_event_via_write(ats_rpc_write_fn write_fn, void *user_data,
                                 uint8_t service, uint8_t command,
                                 const uint8_t *payload, uint16_t payload_length,
                                 uint8_t flags);

int ats_rpc_request(uint8_t service, uint8_t command,
                    const uint8_t *request_payload, uint16_t request_length,
                    uint8_t *response_payload, uint16_t *response_length,
                    uint8_t *response_flags, uint32_t timeout_ms);
int ats_rpc_receive(ats_rpc_frame_t *frame, uint32_t timeout_ms);
void ats_rpc_frame_free(ats_rpc_frame_t *frame);

int ats_rpc_log_event(const char *message);

int ats_rpc_transport_write(const uint8_t *data, uint16_t length, void *user_data);
int ats_rpc_transport_read(uint8_t *byte, uint32_t timeout_ms, void *user_data);
int ats_rpc_log_transport_write(const uint8_t *data, uint16_t length, void *user_data);
int ats_rpc_lcd_transport_write(const uint8_t *data, uint16_t length, void *user_data);

#ifdef __cplusplus
}
#endif
