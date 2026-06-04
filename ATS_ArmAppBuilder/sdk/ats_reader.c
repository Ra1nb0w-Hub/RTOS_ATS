#include "ats_reader.h"
#include "ats_error.h"
#include "ats_rpc.h"
#include "ats_sys.h"

#include <string.h>

#define ATS_READER_RPC_TIMEOUT_MS           500U
#define ATS_READER_RPC_POWER_ON_MS          2000U
#define ATS_READER_RPC_PICC_ACTIVATE_MS     2000U
#define ATS_READER_RPC_TRANSFER_TIMEOUT_MS  5000U
#define ATS_READER_RESP_HEADER_SIZE         4U
#define ATS_READER_MAX_ATR                  64U
#define ATS_READER_MAX_ATS                  64U
#define ATS_READER_MAX_APDU                 4096U

static int rpc_simple_request(uint8_t command)
{
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_READER, command,
                              NULL, 0,
                              resp, &resp_len, ATS_READER_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    return (int32_t)ats_rpc_read_u32_le(resp);
}

int ats_reader_init(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_INIT);
}

int ats_reader_open(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_OPEN);
}

int ats_reader_close(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_CLOSE);
}

int ats_reader_poll(EMVInterfaceType *card_interface, unsigned int timeout_ms)
{
    uint8_t req[4];
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE + 1];
    uint16_t resp_len = sizeof(resp);
    int ret;

    if (!card_interface)
        return ATS_EC_INVALID_PARAM;

    *card_interface = EMV_INTERFACE_NONE;

    if (timeout_ms < 1000)
        timeout_ms = 1000;

    ats_rpc_write_u32_le(req, (uint32_t)timeout_ms);

    ret = ats_rpc_request(ATS_RPC_SERVICE_READER, ATS_RPC_READER_CMD_POLL,
                          req, sizeof(req),
                          resp, &resp_len, timeout_ms);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < (ATS_READER_RESP_HEADER_SIZE + 1))
        return ATS_EC_BAD_DATA;

    *card_interface = (EMVInterfaceType)resp[ATS_READER_RESP_HEADER_SIZE];
    return 0;
}

int ats_reader_cancel(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_CANCEL);
}

int ats_reader_icc_power_on(unsigned char *atr, size_t *atr_len)
{
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE + 2 + ATS_READER_MAX_ATR];
    uint16_t resp_len = sizeof(resp);
    int ret;

    if (!atr_len)
        return ATS_EC_INVALID_PARAM;

    ret = ats_rpc_request(ATS_RPC_SERVICE_READER, ATS_RPC_READER_CMD_ICC_POWER_ON,
                          NULL, 0,
                          resp, &resp_len, ATS_READER_RPC_POWER_ON_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < (ATS_READER_RESP_HEADER_SIZE + 2))
        return ATS_EC_BAD_DATA;

    uint16_t rsp_atr_len = ats_rpc_read_u16_le(&resp[ATS_READER_RESP_HEADER_SIZE]);

    if (!atr || *atr_len < (size_t)rsp_atr_len)
    {
        *atr_len = (size_t)rsp_atr_len;
        return 0;
    }

    if (rsp_atr_len > 0)
        (void)memcpy(atr, &resp[ATS_READER_RESP_HEADER_SIZE + 2], rsp_atr_len);

    *atr_len = (size_t)rsp_atr_len;
    return 0;
}

int ats_reader_icc_power_off(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_ICC_POWER_OFF);
}

int ats_reader_icc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len)
{
    uint16_t req_len;
    uint8_t *req;
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE + 4 + ATS_READER_MAX_APDU];
    uint16_t resp_len = sizeof(resp);
    int ret;

    if (!command || command_len == 0U || !response_len)
        return ATS_EC_INVALID_PARAM;

    if (command_len > (size_t)(0xFFFFU - 4U))
        return ATS_EC_TOO_LARGE;

    req_len = (uint16_t)(4U + (uint16_t)command_len);
    req = (uint8_t *)ats_malloc(req_len);
    if (!req)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u32_le(req, (uint32_t)command_len);
    (void)memcpy(&req[4], command, command_len);

    ret = ats_rpc_request(ATS_RPC_SERVICE_READER, ATS_RPC_READER_CMD_ICC_TRANSCEIVE_APDU,
                          req, req_len,
                          resp, &resp_len, ATS_READER_RPC_TRANSFER_TIMEOUT_MS);
    ats_free(req);

    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < (ATS_READER_RESP_HEADER_SIZE + 4))
        return ATS_EC_BAD_DATA;

    uint32_t rsp_len = ats_rpc_read_u32_le(&resp[ATS_READER_RESP_HEADER_SIZE]);

    if (!response || *response_len < (size_t)rsp_len)
    {
        *response_len = (size_t)rsp_len;
        return 0;
    }

    if (rsp_len > 0)
        (void)memcpy(response, &resp[ATS_READER_RESP_HEADER_SIZE + 4], (size_t)rsp_len);

    *response_len = (size_t)rsp_len;
    return 0;
}

int ats_reader_picc_activate(unsigned char *ats, size_t *ats_len)
{
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE + 2 + ATS_READER_MAX_ATS];
    uint16_t resp_len = sizeof(resp);
    int ret;

    if (!ats_len)
        return ATS_EC_INVALID_PARAM;

    ret = ats_rpc_request(ATS_RPC_SERVICE_READER, ATS_RPC_READER_CMD_PICC_ACTIVATE,
                          NULL, 0,
                          resp, &resp_len, ATS_READER_RPC_PICC_ACTIVATE_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < (ATS_READER_RESP_HEADER_SIZE + 2))
        return ATS_EC_BAD_DATA;

    uint16_t rsp_ats_len = ats_rpc_read_u16_le(&resp[ATS_READER_RESP_HEADER_SIZE]);

    if (!ats || *ats_len < (size_t)rsp_ats_len)
    {
        *ats_len = (size_t)rsp_ats_len;
        return 0;
    }

    if (rsp_ats_len > 0)
        (void)memcpy(ats, &resp[ATS_READER_RESP_HEADER_SIZE + 2], rsp_ats_len);

    *ats_len = (size_t)rsp_ats_len;
    return 0;
}

int ats_reader_picc_deactivate(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_PICC_DEACTIVATE);
}

int ats_reader_picc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len)
{
    uint16_t req_len;
    uint8_t *req;
    uint8_t resp[ATS_READER_RESP_HEADER_SIZE + 4 + ATS_READER_MAX_APDU];
    uint16_t resp_len = sizeof(resp);
    int ret;

    if (!command || command_len == 0U || !response_len)
        return ATS_EC_INVALID_PARAM;

    if (command_len > (size_t)(0xFFFFU - 4U))
        return ATS_EC_TOO_LARGE;

    req_len = (uint16_t)(4U + (uint16_t)command_len);
    req = (uint8_t *)ats_malloc(req_len);
    if (!req)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u32_le(req, (uint32_t)command_len);
    (void)memcpy(&req[4], command, command_len);

    ret = ats_rpc_request(ATS_RPC_SERVICE_READER, ATS_RPC_READER_CMD_PICC_TRANSCEIVE_APDU,
                          req, req_len,
                          resp, &resp_len, ATS_READER_RPC_TRANSFER_TIMEOUT_MS);
    ats_free(req);

    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < ATS_READER_RESP_HEADER_SIZE)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < (ATS_READER_RESP_HEADER_SIZE + 4))
        return ATS_EC_BAD_DATA;

    uint32_t rsp_len = ats_rpc_read_u32_le(&resp[ATS_READER_RESP_HEADER_SIZE]);

    if (!response || *response_len < (size_t)rsp_len)
    {
        *response_len = (size_t)rsp_len;
        return 0;
    }

    if (rsp_len > 0)
        (void)memcpy(response, &resp[ATS_READER_RESP_HEADER_SIZE + 4], (size_t)rsp_len);

    *response_len = (size_t)rsp_len;
    return 0;
}

int ats_reader_get_last_hw_error(void)
{
    return rpc_simple_request(ATS_RPC_READER_CMD_GET_LAST_HW_ERROR);
}
