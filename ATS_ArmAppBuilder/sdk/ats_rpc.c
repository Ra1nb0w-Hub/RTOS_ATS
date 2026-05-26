#include "ats_rpc.h"
#include "ats_error.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/portable.h"
#include <string.h>

static ats_rpc_transport_t s_transport;
static bool s_initialized = false;

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
                                            const uint8_t *payload, uint16_t payload_length)
{
    const uint16_t data_length = (uint16_t)(1U + payload_length);
    const uint16_t frame_length = (uint16_t)(ATS_RPC_HEADER_SIZE + data_length);
    uint8_t *frame_buffer;
    int status;

    (void)flags;

    if ((payload == NULL) && (payload_length != 0U))
    {
        return ATS_EC_INVALID_PARAM;
    }

    if (write_fn == NULL)
    {
        return ATS_RPC_EC_BAD_STATE;
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
    ats_rpc_write_u16_le(&frame_buffer[4], data_length);
    frame_buffer[ATS_RPC_HEADER_SIZE] = command;

    if (payload_length != 0U)
    {
        (void)memcpy(&frame_buffer[ATS_RPC_HEADER_SIZE + 1U], payload, payload_length);
    }

    status = write_fn(frame_buffer, frame_length, user_data);
    vPortFree(frame_buffer);
    return status;
}

static int ats_rpc_send_internal(uint8_t frame_type, uint8_t flags,
                                 uint8_t service, uint8_t command,
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
                         uint8_t flags)
{
    return ats_rpc_send_internal(ATS_RPC_FRAME_TYPE_REQUEST,
                                 flags,
                                 service,
                                 command,
                                 payload,
                                 payload_length);
}

int ats_rpc_send_response(uint8_t service, uint8_t command,
                          const uint8_t *payload, uint16_t payload_length,
                          uint8_t flags)
{
    return ats_rpc_send_internal(ATS_RPC_FRAME_TYPE_RESPONSE,
                                 flags,
                                 service,
                                 command,
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
                                            payload,
                                            payload_length);
}

int ats_rpc_receive(ats_rpc_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t header[ATS_RPC_HEADER_SIZE];
    uint16_t data_length;
    uint8_t command;
    uint16_t payload_offset;
    uint16_t payload_length;
    uint8_t *data_buffer;
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

    data_length = ats_rpc_read_u16_le(&header[4]);
    if (data_length == 0U)
    {
        return ATS_RPC_EC_FRAME;
    }

    data_buffer = (uint8_t *)pvPortMalloc(data_length);
    if (data_buffer == NULL)
    {
        return ATS_RPC_EC_NO_MEMORY;
    }

    status = ats_rpc_read_exact(data_buffer, data_length, timeout_ms);
    if (status != ATS_EC_OK)
    {
        vPortFree(data_buffer);
        return status;
    }

    command = data_buffer[0];
    payload_offset = 1U;
    payload_length = (uint16_t)(data_length - payload_offset);

    frame->frame_type = header[2];
    frame->flags = 0U;
    frame->service = header[3];
    frame->command = command;
    frame->payload_length = payload_length;
    frame->payload = data_buffer + payload_offset;

    return ATS_EC_OK;
}

int ats_rpc_request(uint8_t service, uint8_t command,
                    const uint8_t *request_payload, uint16_t request_length,
                    uint8_t *response_payload, uint16_t *response_length,
                    uint8_t *response_flags, uint32_t timeout_ms)
{
    ats_rpc_frame_t frame;
    int status;

    if ((response_length == NULL) || ((response_payload == NULL) && (*response_length != 0U)))
    {
        return ATS_EC_INVALID_PARAM;
    }

    status = ats_rpc_send_request(service, command, request_payload, request_length, 0U);
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
        if (response_flags != NULL)
        {
            *response_flags = frame.flags;
        }
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

    vPortFree(frame->payload - 1U);
    frame->payload = NULL;
    frame->payload_length = 0U;
}

int ats_rpc_log_event(const char *message)
{
    if (message == NULL || message[0] == '\0')
    {
        return ATS_EC_INVALID_PARAM;
    }

    return ats_rpc_send_event_via_write(ats_rpc_log_transport_write,
                                        NULL,
                                        ATS_RPC_SERVICE_LOG,
                                        ATS_RPC_LOG_CMD_WRITE,
                                        (const uint8_t *)message,
                                        (uint16_t)strlen(message),
                                        0U);
}
