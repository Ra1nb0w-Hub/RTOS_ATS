/**
 * @file ats_net.c
 * @brief ATS网络功能 - RPC代理实现（Target端）
 *
 * 所有网络操作通过 RPC 请求转发到 Host 端执行。
 * Host 端收到消息后调用其本地的 ats_net.c 实现。
 */

#include "ats_net.h"

#include <string.h>
#include <stdlib.h>

#include "ats_rpc.h"
#include "ats_error.h"

#define ATS_NET_RPC_TIMEOUT_MS       5000U
#define ATS_NET_RPC_OVERHEAD_MS      500U

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0]
         | ((uint32_t)buffer[1] << 8)
         | ((uint32_t)buffer[2] << 16)
         | ((uint32_t)buffer[3] << 24);
}

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
}

/* =========================================================
 *  Socket 接口 (RPC 代理)
 * ========================================================= */

int ats_sock_create(ats_sock_t *sock, ats_sock_family_t family, ats_sock_type_t type, ats_sock_protocol_t protocol)
{
    if (!sock)
        return -1;

    uint8_t req[3];
    req[0] = (uint8_t)family;
    req[1] = (uint8_t)type;
    req[2] = (uint8_t)protocol;

    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);
    int rpc_ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CREATE,
                                  req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (rpc_ret != ATS_EC_OK || resp_len < 4)
        return -1;

    int32_t ret = (int32_t)read_u32_le(resp);
    if (ret != 0)
        return (int)ret;

    if (resp_len < 8)
        return -1;

    *sock = (ats_sock_t)(int32_t)read_u32_le(&resp[4]);
    return 0;
}

int ats_sock_connect(ats_sock_t sock, const char *host, uint16_t port, unsigned int timeout_ms)
{
    if (!host)
        return -1;

    uint16_t host_len = (uint16_t)strlen(host);
    if (host_len > 255U)
        return -1;

    uint16_t req_len = (uint16_t)(4 + 1 + host_len + 2 + 4);
    uint8_t *req = (uint8_t *)malloc(req_len);
    if (!req)
        return -1;

    write_u32_le(req, (uint32_t)sock);
    req[4] = (uint8_t)host_len;
    memcpy(&req[5], host, host_len);
    write_u16_le(&req[5 + host_len], port);
    write_u32_le(&req[5 + host_len + 2], (uint32_t)timeout_ms);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);

    uint32_t rpc_timeout = timeout_ms;
    if (rpc_timeout > 0xFFFFFFFFU - ATS_NET_RPC_OVERHEAD_MS)
        rpc_timeout = 0xFFFFFFFFU;
    else
        rpc_timeout += ATS_NET_RPC_OVERHEAD_MS;

    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CONNECT,
                              req, req_len, resp, &resp_len, rpc_timeout);
    free(req);

    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_sock_send(ats_sock_t sock, const void *buf, unsigned int len)
{
    if (!buf || len == 0U)
        return -1;

    if (len > 0xFFFFU - 4U)
        len = 0xFFFFU - 4U;

    uint16_t req_len = (uint16_t)(4 + len);
    uint8_t *req = (uint8_t *)malloc(req_len);
    if (!req)
        return -1;

    write_u32_le(req, (uint32_t)sock);
    memcpy(&req[4], buf, len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_SEND,
                              req, req_len, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    free(req);

    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_sock_recv(ats_sock_t sock, void *buf, unsigned int len, unsigned int timeout_ms)
{
    if (!buf || len == 0U)
        return -1;

    if (len > 0xFFFFU - 4U)
        len = 0xFFFFU - 4U;

    uint8_t req[12];
    write_u32_le(req, (uint32_t)sock);
    write_u32_le(&req[4], len);
    write_u32_le(&req[8], timeout_ms);

    uint16_t resp_len = (uint16_t)(4 + len);
    uint8_t *resp = (uint8_t *)malloc(resp_len);
    if (!resp)
        return -1;

    uint32_t rpc_timeout = timeout_ms;
    if (rpc_timeout > 0xFFFFFFFFU - ATS_NET_RPC_OVERHEAD_MS)
        rpc_timeout = 0xFFFFFFFFU;
    else
        rpc_timeout += ATS_NET_RPC_OVERHEAD_MS;

    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_RECV,
                              req, sizeof(req), resp, &resp_len, rpc_timeout);
    if (ret != ATS_EC_OK || resp_len < 4)
    {
        free(resp);
        return -1;
    }

    int32_t bytes_recv = (int32_t)read_u32_le(resp);
    if (bytes_recv > 0 && (uint16_t)(4 + bytes_recv) <= resp_len)
    {
        memcpy(buf, &resp[4], (size_t)bytes_recv);
    }
    else if (bytes_recv < 0)
    {
        free(resp);
        return -1;
    }

    free(resp);
    return (int)bytes_recv;
}

int ats_sock_close(ats_sock_t sock)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)sock);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CLOSE,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

/* =========================================================
 *  网络模式管理 (RPC 代理)
 * ========================================================= */

int ats_net_set_mode(ats_net_mode_t mode)
{
    uint8_t req[1];
    req[0] = (uint8_t)mode;

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SET_MODE,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

ats_net_mode_t ats_net_get_mode(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_GET_MODE,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return ATS_NET_MODE_CELLUALR;

    return (ats_net_mode_t)read_u32_le(resp);
}

int ats_net_set_status(bool status)
{
    uint8_t req[1];
    req[0] = status ? 1U : 0U;

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SET_STATUS,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

bool ats_net_get_status(void)
{
    uint8_t resp[1];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_GET_STATUS,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 1)
        return false;

    return resp[0] != 0U;
}

/* =========================================================
 *  WiFi状态管理 (RPC 代理)
 * ========================================================= */

int ats_net_wifi_set_module_status(bool status)
{
    uint8_t req[1];
    req[0] = status ? 1U : 0U;

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_SET_MODULE_STATUS,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

bool ats_net_wifi_get_module_status(void)
{
    uint8_t resp[1];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_MODULE_STATUS,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 1)
        return false;

    return resp[0] != 0U;
}

int ats_net_wifi_set_ssid(const char *ssid)
{
    if (!ssid)
        return -1;

    uint16_t ssid_len = (uint16_t)strlen(ssid);
    if (ssid_len > 63U)
        ssid_len = 63U;

    uint8_t req[65];
    req[0] = (uint8_t)ssid_len;
    memcpy(&req[1], ssid, ssid_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_SET_SSID,
                              req, (uint16_t)(1 + ssid_len),
                              resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

char *ats_net_wifi_get_ssid(void)
{
    uint8_t resp[64];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_SSID,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len == 0U)
        return NULL;

    if (resp_len > 63U)
        resp_len = 63U;

    char *ssid = (char *)malloc((size_t)resp_len + 1U);
    if (!ssid)
        return NULL;

    memcpy(ssid, resp, resp_len);
    ssid[resp_len] = '\0';
    return ssid;
}

int ats_net_wifi_set_signal(int signal)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)signal);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_SET_SIGNAL,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_wifi_get_signal(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_SIGNAL,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_wifi_set_ap_list(ats_net_wifi_ap_t *ap_list, unsigned int count)
{
    if ((!ap_list && count > 0U) || count > 0xFFFFU)
        return -1;

#define ATS_NET_WIFI_AP_SERIAL_SIZE  92U
    uint32_t req_len;
    uint16_t count_u16;

    if (count == 0U)
    {
        req_len = 2U;
    }
    else
    {
        req_len = 2U + (uint32_t)count * ATS_NET_WIFI_AP_SERIAL_SIZE;
    }

    if (req_len > 0xFFFFU)
        return -1;

    uint8_t *req = (uint8_t *)malloc((size_t)req_len);
    if (!req)
        return -1;

    count_u16 = (uint16_t)count;
    write_u16_le(req, count_u16);

    for (uint16_t i = 0U; i < count_u16; i++)
    {
        uint16_t offset = (uint16_t)(2U + (uint16_t)i * ATS_NET_WIFI_AP_SERIAL_SIZE);
        memcpy(&req[offset], ap_list[i].ssid, 64U);
        write_u32_le(&req[offset + 64U], (uint32_t)ap_list[i].rssi);
        memcpy(&req[offset + 68U], ap_list[i].mac, 24U);
    }

    uint8_t resp[4];
    uint16_t resp_len_out = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_SET_AP_LIST,
                              req, (uint16_t)req_len, resp, &resp_len_out, ATS_NET_RPC_TIMEOUT_MS);
    free(req);

    if (ret != ATS_EC_OK || resp_len_out < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_wifi_get_ap_list(ats_net_wifi_ap_t **ap_list, unsigned int *count)
{
    if (!ap_list || !count)
        return -1;

    uint8_t *resp = (uint8_t *)malloc(0xFFFFU);
    if (!resp)
        return -1;

    uint16_t resp_len = 0xFFFFU;
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_AP_LIST,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4U)
    {
        free(resp);
        *ap_list = NULL;
        *count = 0U;
        return -1;
    }

    int32_t result = (int32_t)read_u32_le(resp);
    if (result != 0)
    {
        free(resp);
        *ap_list = NULL;
        *count = 0U;
        return result;
    }

    if (resp_len < 6U)
    {
        free(resp);
        *ap_list = NULL;
        *count = 0U;
        return -1;
    }

    uint16_t ap_count = read_u16_le(&resp[4]);
    if (ap_count == 0U || (uint32_t)resp_len < (uint32_t)(6U + ap_count * ATS_NET_WIFI_AP_SERIAL_SIZE))
    {
        free(resp);
        *ap_list = NULL;
        *count = 0U;
        return 0;
    }

    *ap_list = (ats_net_wifi_ap_t *)malloc(sizeof(ats_net_wifi_ap_t) * ap_count);
    if (!*ap_list)
    {
        free(resp);
        return -1;
    }

    for (uint16_t i = 0U; i < ap_count; i++)
    {
        uint16_t offset = (uint16_t)(6U + i * ATS_NET_WIFI_AP_SERIAL_SIZE);
        memcpy((*ap_list)[i].ssid, &resp[offset], 64U);
        (*ap_list)[i].ssid[63] = '\0';
        (*ap_list)[i].rssi = (int)read_u32_le(&resp[offset + 64U]);
        memcpy((*ap_list)[i].mac, &resp[offset + 68U], 24U);
        (*ap_list)[i].mac[23] = '\0';
    }

    *count = ap_count;
    free(resp);
    return 0;
}

/* =========================================================
 *  蜂窝网络参数管理 (RPC 代理)
 * ========================================================= */

int ats_net_cellular_set_mcc(int mcc)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)mcc);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_MCC,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_get_mcc(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_MCC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_set_mnc(int mnc)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)mnc);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_MNC,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_get_mnc(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_MNC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_set_lac(int lac)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)lac);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_LAC,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_get_lac(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_LAC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_set_cell_id(int cell_id)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)cell_id);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_CELL_ID,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_get_cell_id(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_CELL_ID,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_set_signal(int signal)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)signal);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_SIGNAL,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_get_signal(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_SIGNAL,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)read_u32_le(resp);
}

int ats_net_cellular_set_imsi(char *imsi)
{
    if (!imsi)
        return -1;

    uint16_t imsi_len = (uint16_t)strlen(imsi);
    if (imsi_len > 31U)
        imsi_len = 31U;

    uint8_t req[33];
    req[0] = (uint8_t)imsi_len;
    memcpy(&req[1], imsi, imsi_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_IMSI,
                              req, (uint16_t)(1 + imsi_len),
                              resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

char *ats_net_cellular_get_imsi(void)
{
    uint8_t resp[32];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_IMSI,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len == 0U)
        return NULL;

    if (resp_len > 31U)
        resp_len = 31U;

    char *imsi = (char *)malloc((size_t)resp_len + 1U);
    if (!imsi)
        return NULL;

    memcpy(imsi, resp, resp_len);
    imsi[resp_len] = '\0';
    return imsi;
}

int ats_net_cellular_set_imei(char *imei)
{
    if (!imei)
        return -1;

    uint16_t imei_len = (uint16_t)strlen(imei);
    if (imei_len > 31U)
        imei_len = 31U;

    uint8_t req[33];
    req[0] = (uint8_t)imei_len;
    memcpy(&req[1], imei, imei_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_SET_IMEI,
                              req, (uint16_t)(1 + imei_len),
                              resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

char *ats_net_cellular_get_imei(void)
{
    uint8_t resp[32];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_IMEI,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len == 0U)
        return NULL;

    if (resp_len > 31U)
        resp_len = 31U;

    char *imei = (char *)malloc((size_t)resp_len + 1U);
    if (!imei)
        return NULL;

    memcpy(imei, resp, resp_len);
    imei[resp_len] = '\0';
    return imei;
}
