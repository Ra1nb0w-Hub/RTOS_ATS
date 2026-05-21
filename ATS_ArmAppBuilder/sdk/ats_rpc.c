#include "ats_rpc.h"
#include "ats_error.h"

#include <string.h>

#define ATS_RPC_HEADER_SIZE            11U
#define ATS_RPC_CRC_SIZE               2U
#define ATS_RPC_MAX_FRAME_SIZE         (ATS_RPC_HEADER_SIZE + ATS_RPC_MAX_PAYLOAD + ATS_RPC_CRC_SIZE)
#define ATS_RPC_LOG_LEVEL_MAX          31U
#define ATS_RPC_LOG_MESSAGE_MAX        (ATS_RPC_MAX_PAYLOAD - 3U - ATS_RPC_LOG_LEVEL_MAX)

static ats_rpc_transport_t s_transport;
static bool s_initialized = false;
static uint16_t s_next_request_id = 1U;

int __attribute__((weak)) ats_rpc_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)data;
    (void)length;
    (void)user_data;
    return ATS_EC_INVALID_PARAM;
}

int __attribute__((weak)) ats_rpc_transport_read(uint8_t *byte, uint32_t timeout_ms, void *user_data)
{
    (void)byte;
    (void)timeout_ms;
    (void)user_data;
    return ATS_EC_TIMEOUT;
}

int __attribute__((weak)) ats_rpc_log_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)data;
    (void)length;
    (void)user_data;
    return ATS_EC_INVALID_PARAM;
}

int __attribute__((weak)) ats_rpc_lcd_transport_write(const uint8_t *data, uint16_t length, void *user_data)
{
    (void)data;
    (void)length;
    (void)user_data;
    return ATS_EC_INVALID_PARAM;
}

static void ats_rpc_write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t ats_rpc_read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

static int ats_rpc_read_exact(uint8_t *buffer, uint16_t length, uint32_t timeout_ms)
{
    uint16_t index;

    if ((buffer == NULL) && (length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if ((!s_initialized) || (s_transport.read == NULL))
    {
        return ATS_RPC_EC_BAD_STATE;
    }

    for (index = 0U; index < length; ++index)
    {
        const int status = s_transport.read(&buffer[index], timeout_ms, s_transport.user_data);
        if (status != ATS_EC_OK)
        {
            return status;
        }
    }

    return ATS_EC_OK;
}

static int ats_rpc_send_internal_with_write(ats_rpc_write_fn write_fn, void *user_data,
                                            uint8_t frame_type, uint8_t flags,
                                            uint8_t service, uint8_t command,
                                            uint16_t request_id,
                                            const uint8_t *payload, uint16_t payload_length)
{
    uint8_t frame_buffer[ATS_RPC_MAX_FRAME_SIZE];
    uint16_t crc;
    uint16_t frame_length;

    if ((payload == NULL) && (payload_length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (payload_length > ATS_RPC_MAX_PAYLOAD)
    {
        return ATS_RPC_EC_TOO_LARGE;
    }

    if (write_fn == NULL)
    {
        return ATS_RPC_EC_BAD_STATE;
    }

    frame_buffer[0] = ATS_RPC_SOF0;
    frame_buffer[1] = ATS_RPC_SOF1;
    frame_buffer[2] = ATS_RPC_VERSION;
    frame_buffer[3] = frame_type;
    frame_buffer[4] = flags;
    frame_buffer[5] = service;
    frame_buffer[6] = command;
    ats_rpc_write_u16_le(&frame_buffer[7], request_id);
    ats_rpc_write_u16_le(&frame_buffer[9], payload_length);

    if (payload_length != 0U)
    {
        (void)memcpy(&frame_buffer[ATS_RPC_HEADER_SIZE], payload, payload_length);
    }

    crc = ats_rpc_crc16_ccitt(&frame_buffer[2], (uint16_t)(ATS_RPC_HEADER_SIZE - 2U + payload_length));
    ats_rpc_write_u16_le(&frame_buffer[ATS_RPC_HEADER_SIZE + payload_length], crc);

    frame_length = (uint16_t)(ATS_RPC_HEADER_SIZE + payload_length + ATS_RPC_CRC_SIZE);
    return write_fn(frame_buffer, frame_length, user_data);
}

static int ats_rpc_send_internal(uint8_t frame_type, uint8_t flags,
                                 uint8_t service, uint8_t command,
                                 uint16_t request_id,
                                 const uint8_t *payload, uint16_t payload_length)
{
    if ((!s_initialized) || (s_transport.write == NULL))
    {
        return ATS_RPC_EC_BAD_STATE;
    }

    return ats_rpc_send_internal_with_write(s_transport.write,
                                            s_transport.user_data,
                                            frame_type,
                                            flags,
                                            service,
                                            command,
                                            request_id,
                                            payload,
                                            payload_length);
}

int ats_rpc_init(const ats_rpc_transport_t *transport)
{
    if ((transport == NULL) || (transport->write == NULL) || (transport->read == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    s_transport = *transport;
    s_initialized = true;
    s_next_request_id = 1U;
    return ATS_EC_OK;
}

int ats_rpc_init_default(void)
{
    const ats_rpc_transport_t transport =
    {
        ats_rpc_transport_write,
        ats_rpc_transport_read,
        NULL
    };

    return ats_rpc_init(&transport);
}

void ats_rpc_deinit(void)
{
    (void)memset(&s_transport, 0, sizeof(s_transport));
    s_initialized = false;
}

bool ats_rpc_is_initialized(void)
{
    return s_initialized;
}

int ats_rpc_send_request(uint8_t service, uint8_t command,
                         const uint8_t *payload, uint16_t payload_length,
                         uint16_t request_id, uint8_t flags)
{
    return ats_rpc_send_internal(ATS_RPC_FRAME_TYPE_REQUEST,
                                 (uint8_t)(flags | ATS_RPC_FLAG_NEED_ACK),
                                 service,
                                 command,
                                 request_id,
                                 payload,
                                 payload_length);
}

int ats_rpc_send_response(uint8_t service, uint8_t command,
                          const uint8_t *payload, uint16_t payload_length,
                          uint16_t request_id, uint8_t flags)
{
    return ats_rpc_send_internal(ATS_RPC_FRAME_TYPE_RESPONSE,
                                 flags,
                                 service,
                                 command,
                                 request_id,
                                 payload,
                                 payload_length);
}

int ats_rpc_send_event(uint8_t service, uint8_t command,
                       const uint8_t *payload, uint16_t payload_length,
                       uint8_t flags)
{
    return ats_rpc_send_internal(ATS_RPC_FRAME_TYPE_EVENT,
                                 flags,
                                 service,
                                 command,
                                 0U,
                                 payload,
                                 payload_length);
}

int ats_rpc_send_event_via_write(ats_rpc_write_fn write_fn, void *user_data,
                                 uint8_t service, uint8_t command,
                                 const uint8_t *payload, uint16_t payload_length,
                                 uint8_t flags)
{
    return ats_rpc_send_internal_with_write(write_fn,
                                            user_data,
                                            ATS_RPC_FRAME_TYPE_EVENT,
                                            flags,
                                            service,
                                            command,
                                            0U,
                                            payload,
                                            payload_length);
}

int ats_rpc_receive(ats_rpc_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t header[ATS_RPC_HEADER_SIZE];
    uint8_t payload_and_crc[ATS_RPC_MAX_PAYLOAD + ATS_RPC_CRC_SIZE];
    uint16_t payload_length;
    uint16_t expected_crc;
    uint16_t actual_crc;
    int status;

    if (frame == NULL)
    {
        return ATS_EC_INVALID_PARAM;
    }

    for (;;)
    {
        status = ats_rpc_read_exact(&header[0], 1U, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if (header[0] != ATS_RPC_SOF0)
        {
            continue;
        }

        status = ats_rpc_read_exact(&header[1], 1U, timeout_ms);
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

    status = ats_rpc_read_exact(&header[2], (uint16_t)(ATS_RPC_HEADER_SIZE - 2U), timeout_ms);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    if (header[2] != ATS_RPC_VERSION)
    {
        return ATS_RPC_EC_FRAME;
    }

    payload_length = ats_rpc_read_u16_le(&header[9]);
    if (payload_length > ATS_RPC_MAX_PAYLOAD)
    {
        return ATS_RPC_EC_TOO_LARGE;
    }

    status = ats_rpc_read_exact(payload_and_crc, (uint16_t)(payload_length + ATS_RPC_CRC_SIZE), timeout_ms);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    expected_crc = ats_rpc_read_u16_le(&payload_and_crc[payload_length]);

    /*
     * CRC is calculated over the bytes after SOF. Rebuild a contiguous view
     * into the frame structure to keep the parser independent from transport.
     */
    {
        uint8_t crc_buffer[ATS_RPC_HEADER_SIZE - 2U + ATS_RPC_MAX_PAYLOAD];
        const uint16_t crc_length = (uint16_t)(ATS_RPC_HEADER_SIZE - 2U + payload_length);

        (void)memcpy(crc_buffer, &header[2], ATS_RPC_HEADER_SIZE - 2U);
        if (payload_length != 0U)
        {
            (void)memcpy(&crc_buffer[ATS_RPC_HEADER_SIZE - 2U], payload_and_crc, payload_length);
        }

        actual_crc = ats_rpc_crc16_ccitt(crc_buffer, crc_length);
    }

    if (actual_crc != expected_crc)
    {
        return ATS_RPC_EC_CRC;
    }

    frame->frame_type = header[3];
    frame->flags = header[4];
    frame->service = header[5];
    frame->command = header[6];
    frame->request_id = ats_rpc_read_u16_le(&header[7]);
    frame->payload_length = payload_length;

    if (payload_length != 0U)
    {
        (void)memcpy(frame->payload, payload_and_crc, payload_length);
    }

    return ATS_EC_OK;
}

int ats_rpc_request(uint8_t service, uint8_t command,
                    const uint8_t *request_payload, uint16_t request_length,
                    uint8_t *response_payload, uint16_t *response_length,
                    uint8_t *response_flags, uint32_t timeout_ms)
{
    ats_rpc_frame_t frame;
    uint16_t request_id;
    int status;

    if ((response_length == NULL) || ((response_payload == NULL) && (*response_length != 0U)))
    {
        return ATS_EC_INVALID_PARAM;
    }

    request_id = ats_rpc_next_request_id();
    status = ats_rpc_send_request(service, command, request_payload, request_length, request_id, 0U);
    if (status != ATS_EC_OK)
    {
        return status;
    }

    for (;;)
    {
        status = ats_rpc_receive(&frame, timeout_ms);
        if (status != ATS_EC_OK)
        {
            return status;
        }

        if ((frame.frame_type != ATS_RPC_FRAME_TYPE_RESPONSE) ||
            (frame.service != service) ||
            (frame.command != command) ||
            (frame.request_id != request_id))
        {
            continue;
        }

        if (frame.payload_length > *response_length)
        {
            return ATS_RPC_EC_TOO_LARGE;
        }

        if (frame.payload_length != 0U)
        {
            (void)memcpy(response_payload, frame.payload, frame.payload_length);
        }

        *response_length = frame.payload_length;
        if (response_flags != NULL)
        {
            *response_flags = frame.flags;
        }
        return ATS_EC_OK;
    }
}

uint16_t ats_rpc_crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint16_t ats_rpc_next_request_id(void)
{
    if (s_next_request_id == 0U)
    {
        s_next_request_id = 1U;
    }

    return s_next_request_id++;
}

int ats_rpc_log_event(const char *level, const char *message)
{
    uint8_t payload[ATS_RPC_MAX_PAYLOAD];
    size_t level_length;
    size_t message_length;
    uint16_t total_length;

    if ((level == NULL) || (message == NULL))
    {
        return ATS_EC_INVALID_PARAM;
    }

    level_length = strlen(level);
    message_length = strlen(message);

    if (level_length > ATS_RPC_LOG_LEVEL_MAX)
    {
        level_length = ATS_RPC_LOG_LEVEL_MAX;
    }

    if (message_length > ATS_RPC_LOG_MESSAGE_MAX)
    {
        message_length = ATS_RPC_LOG_MESSAGE_MAX;
    }

    payload[0] = (uint8_t)level_length;
    (void)memcpy(&payload[1], level, level_length);
    ats_rpc_write_u16_le(&payload[1U + level_length], (uint16_t)message_length);
    (void)memcpy(&payload[3U + level_length], message, message_length);

    total_length = (uint16_t)(3U + level_length + message_length);
    return ats_rpc_send_event_via_write(ats_rpc_log_transport_write,
                                        NULL,
                                        ATS_RPC_SERVICE_LOG,
                                        ATS_RPC_LOG_CMD_WRITE,
                                        payload,
                                        total_length,
                                        0U);
}
