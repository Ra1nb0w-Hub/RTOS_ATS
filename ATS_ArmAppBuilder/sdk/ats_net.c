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
#include "ats_sys.h"

#define ATS_NET_RPC_TIMEOUT_MS       5000U
#define ATS_NET_RPC_OVERHEAD_MS      500U
#define ATS_NET_WIFI_AP_SERIAL_SIZE  92U

static ats_net_mode_t s_net_mode = ATS_NET_MODE_CELLUALR;
static bool s_net_status = true;
static bool s_wifi_module_status = true;

static void _net_mode_change_callback(ats_net_mode_t mode)
{
    s_net_mode = mode;
}

static void _net_status_change_callback(bool status)
{
    s_net_status = status;
}

static void _wifi_module_status_change_callback(bool status)
{
    s_wifi_module_status = status;
}

int ats_net_rpc_register_callback(ats_net_rpc_callback_t *callback)
{
    if (!callback)
        return ATS_EC_INVALID_PARAM;

    callback->net_mode_change = _net_mode_change_callback;
    callback->net_status_change = _net_status_change_callback;
    callback->wifi_module_status_change = _wifi_module_status_change_callback;

    return ATS_EC_OK;
}

/* =========================================================
 *  Socket 接口 (RPC 代理)
 * ========================================================= */

int ats_sock_create(ats_sock_t *sock, ats_sock_family_t family, ats_sock_type_t type, ats_sock_protocol_t protocol)
{
    if (!sock)
        return ATS_EC_INVALID_PARAM;

    uint8_t req[3];
    req[0] = (uint8_t)family;
    req[1] = (uint8_t)type;
    req[2] = (uint8_t)protocol;

    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);
    int rpc_ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CREATE,
                                  req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (rpc_ret != ATS_EC_OK)
        return rpc_ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    int32_t ret = (int32_t)ats_rpc_read_u32_le(resp);
    if (ret != 0)
        return (int)ret;

    if (resp_len < 8)
        return ATS_EC_BAD_DATA;

    *sock = (ats_sock_t)(int32_t)ats_rpc_read_u32_le(&resp[4]);
    return 0;
}

int ats_sock_connect(ats_sock_t sock, const char *host, uint16_t port, unsigned int timeout_ms)
{
    if (!host)
        return ATS_EC_INVALID_PARAM;

    uint16_t host_len = (uint16_t)strlen(host);
    if (host_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint16_t req_len = (uint16_t)(4 + 1 + host_len + 2 + 4);
    uint8_t *req = (uint8_t *)ats_malloc(req_len);
    if (!req)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u32_le(req, (uint32_t)sock);
    req[4] = (uint8_t)host_len;
    memcpy(&req[5], host, host_len);
    ats_rpc_write_u16_le(&req[5 + host_len], port);
    ats_rpc_write_u32_le(&req[5 + host_len + 2], (uint32_t)timeout_ms);

    /* 响应格式: [sock(4)][ret(4)] */
    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);

    uint32_t rpc_timeout = timeout_ms;
    if (rpc_timeout > 0xFFFFFFFFU - ATS_NET_RPC_OVERHEAD_MS)
        rpc_timeout = 0xFFFFFFFFU;
    else
        rpc_timeout += ATS_NET_RPC_OVERHEAD_MS;

    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CONNECT,
                                  req, req_len, resp, &resp_len, rpc_timeout);
    ats_free(req);

    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 8)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(&resp[4]);
}

int ats_sock_send(ats_sock_t sock, const void *buf, unsigned int len)
{
    if (!buf || len == 0U)
        return ATS_EC_INVALID_PARAM;

    if (len > 0xFFFFU - 4U)
        len = 0xFFFFU - 4U;

    uint16_t req_len = (uint16_t)(4 + len);
    uint8_t *req = (uint8_t *)ats_malloc(req_len);
    if (!req)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u32_le(req, (uint32_t)sock);
    memcpy(&req[4], buf, len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_SEND,
                              req, req_len, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    ats_free(req);

    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_sock_recv(ats_sock_t sock, void *buf, unsigned int len, unsigned int timeout_ms)
{
    if (!buf || len == 0U)
        return ATS_EC_INVALID_PARAM;

    if (len > 0xFFFFU - 4U)
        len = 0xFFFFU - 4U;

    uint8_t req[12];
    ats_rpc_write_u32_le(req, (uint32_t)sock);
    ats_rpc_write_u32_le(&req[4], len);
    ats_rpc_write_u32_le(&req[8], timeout_ms);

    /* 响应格式: [sock(4)][bytes_recv(4)][data...] */
    /* 防止 uint16_t 溢出: 8 + len 最大为 8 + 65531 = 65539, 超出 uint16_t */
    uint32_t alloc_len = 8U + (uint32_t)len;
    if (alloc_len > 0xFFFFU)
        alloc_len = 0xFFFFU;
    uint16_t resp_len = (uint16_t)alloc_len;
    uint8_t *resp = (uint8_t *)ats_malloc(resp_len);
    if (!resp)
        return ATS_EC_NO_MEMORY;

    uint32_t rpc_timeout = timeout_ms;
    if (rpc_timeout > 0xFFFFFFFFU - ATS_NET_RPC_OVERHEAD_MS)
        rpc_timeout = 0xFFFFFFFFU;
    else
        rpc_timeout += ATS_NET_RPC_OVERHEAD_MS;

    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_RECV,
                                  req, sizeof(req), resp, &resp_len, rpc_timeout);
    if (ret != ATS_EC_OK)
    {
        ats_free(resp);
        return ret;
    }
    if (resp_len < 8)
    {
        ats_free(resp);
        return ATS_EC_BAD_DATA;
    }

    int32_t bytes_recv = (int32_t)ats_rpc_read_u32_le(&resp[4]);

    /* bytes_recv <= 0: 连接关闭/超时/错误, 返回 bytes_recv 告知上层具体原因 */
    if (bytes_recv <= 0)
    {
        ats_free(resp);
        return (int)bytes_recv;
    }

    /* 校验实际响应长度是否包含完整数据, 防止截断 */
    if ((uint16_t)(8U + (uint32_t)bytes_recv) > resp_len)
    {
        ats_free(resp);
        return ATS_EC_BAD_DATA;
    }

    memcpy(buf, &resp[8], (size_t)bytes_recv);
    ats_free(resp);
    return (int)bytes_recv;
}

int ats_sock_close(ats_sock_t sock)
{
    uint8_t req[4];
    ats_rpc_write_u32_le(req, (uint32_t)sock);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_SOCK_CLOSE,
                              req, sizeof(req), resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
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

    ret = (int)ats_rpc_read_u32_le(resp);
    if (ret == ATS_EC_OK)
        s_net_mode = mode;

    return ret;
}

ats_net_mode_t ats_net_get_mode(void)
{
    return s_net_mode;
}

bool ats_net_get_status(void)
{
    return s_net_status;
}

/* =========================================================
 *  WiFi状态管理 (RPC 代理)
 * ========================================================= */

bool ats_net_wifi_get_module_status(void)
{
    return s_wifi_module_status;
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

    char *ssid = (char *)ats_malloc((size_t)resp_len + 1U);
    if (!ssid)
        return NULL;

    memcpy(ssid, resp, resp_len);
    ssid[resp_len] = '\0';
    return ssid;
}

int ats_net_wifi_get_signal(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_SIGNAL,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_net_wifi_get_ap_list(ats_net_wifi_ap_t **ap_list, unsigned int *count)
{
    if (!ap_list || !count)
        return ATS_EC_INVALID_PARAM;

    uint8_t *resp = (uint8_t *)ats_malloc(0xFFFFU);
    if (!resp)
        return ATS_EC_NO_MEMORY;

    uint16_t resp_len = 0xFFFFU;
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_WIFI_GET_AP_LIST,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
    {
        ats_free(resp);
        *ap_list = NULL;
        *count = 0U;
        return ret;
    }
    if (resp_len < 4U)
    {
        ats_free(resp);
        *ap_list = NULL;
        *count = 0U;
        return ATS_EC_BAD_DATA;
    }

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
    {
        ats_free(resp);
        *ap_list = NULL;
        *count = 0U;
        return result;
    }

    if (resp_len < 6U)
    {
        ats_free(resp);
        *ap_list = NULL;
        *count = 0U;
        return ATS_EC_BAD_DATA;
    }

    uint16_t ap_count = ats_rpc_read_u16_le(&resp[4]);
    if (ap_count == 0U || (uint32_t)resp_len < (uint32_t)(6U + ap_count * ATS_NET_WIFI_AP_SERIAL_SIZE))
    {
        ats_free(resp);
        *ap_list = NULL;
        *count = 0U;
        return 0;
    }

    *ap_list = (ats_net_wifi_ap_t *)ats_malloc(sizeof(ats_net_wifi_ap_t) * ap_count);
    if (!*ap_list)
    {
        ats_free(resp);
        return ATS_EC_NO_MEMORY;
    }

    for (uint16_t i = 0U; i < ap_count; i++)
    {
        uint16_t offset = (uint16_t)(6U + i * ATS_NET_WIFI_AP_SERIAL_SIZE);
        memcpy((*ap_list)[i].ssid, &resp[offset], 64U);
        (*ap_list)[i].ssid[63] = '\0';
        (*ap_list)[i].rssi = (int)ats_rpc_read_u32_le(&resp[offset + 64U]);
        memcpy((*ap_list)[i].mac, &resp[offset + 68U], 24U);
        (*ap_list)[i].mac[23] = '\0';
    }

    *count = ap_count;
    ats_free(resp);
    return 0;
}

/* =========================================================
 *  蜂窝网络参数管理 (RPC 代理)
 * ========================================================= */

int ats_net_cellular_get_mcc(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_MCC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_net_cellular_get_mnc(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_MNC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_net_cellular_get_lac(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_LAC,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_net_cellular_get_cell_id(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_CELL_ID,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_net_cellular_get_signal(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_SIGNAL,
                              NULL, 0, resp, &resp_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return 0;

    return (int)ats_rpc_read_u32_le(resp);
}

char *ats_net_cellular_get_imsi(void)
{
    static char imsi[32] = {0};
    uint16_t imsi_len = sizeof(imsi);

    memset(imsi, 0, imsi_len);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_IMSI,
                              NULL, 0, (uint8_t *)imsi, &imsi_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || imsi_len == 0U)
        return NULL;

    return imsi;
}

char *ats_net_cellular_get_imei(void)
{
    static char imei[32] = {0};
    uint16_t imei_len = sizeof(imei);

    memset(imei, 0, imei_len);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_NET, ATS_RPC_NET_CMD_CELLULAR_GET_IMEI,
                              NULL, 0, (uint8_t *)imei, &imei_len, ATS_NET_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || imei_len == 0U)
        return NULL;

    return imei;
}
