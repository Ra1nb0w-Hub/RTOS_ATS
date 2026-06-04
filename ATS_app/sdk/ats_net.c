/**
 * @file ats_net.c
 * @brief ATS网络功能 - Windows平台实现
 *
 * 使用 Winsock2 实现 TCP/UDP socket 接口；
 * TLS连接使用 mbedtls 库（见 ats/lib/mbedtls）。
 * 蜂窝/WiFi 状态均通过变量保存（Windows无实际硬件）。
 */

#include "ats_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---- Winsock ---- */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

/* ---- mbedtls（TLS支持）----
 * 嵌入式目标：SDK 已提供 sdk/inc/600MTLS/mbedtls/include 作为 include 路径
 * Windows 平台：需将 ats/lib/mbedtls/include 加入编译器 include 路径
 */
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

/* =========================================================
 * 内部状态变量
 * ========================================================= */

static ats_net_rpc_callback_t s_net_rpc_callback = {0};
static bool s_net_status = true;
static ats_net_mode_t s_net_mode = ATS_NET_MODE_CELLUALR;

static bool s_wifi_module_status = true;
static char s_wifi_cur_ssid[64] = {0};
static int s_wifi_cur_signal = 0;
static ats_net_wifi_ap_t *s_wifi_ap_list = NULL;
static size_t s_wifi_ap_count = 0;

static int s_cellular_mcc = 0;
static int s_cellular_mnc = 0;
static int s_cellular_lac = 0;
static int s_cellular_cell_id = 0;
static int s_cellular_signal = 0;
static char s_cellular_imsi[32] = {0};
static char s_cellular_imei[32] = {0};

/* Winsock初始化标志 */
static bool s_winsock_initialized = false;

/* =========================================================
 * TLS 上下文池
 * ========================================================= */

#define ATS_NET_MAX_SOCKETS 32

typedef struct
{
    bool in_use;
    bool is_tls;
    /* 普通 socket */
    SOCKET raw_sock;
    /* TLS上下文 */
    mbedtls_net_context tls_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config ssl_conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
} ats_net_ctx_t;

static ats_net_ctx_t s_net_ctx[ATS_NET_MAX_SOCKETS];
static bool s_net_ctx_init = false;

static void net_ctx_pool_init(void)
{
    if (!s_net_ctx_init)
    {
        memset(s_net_ctx, 0, sizeof(s_net_ctx));
        for (int i = 0; i < ATS_NET_MAX_SOCKETS; i++)
        {
            s_net_ctx[i].raw_sock = INVALID_SOCKET;
        }
        s_net_ctx_init = true;
    }
}

static int alloc_net_ctx(void)
{
    for (int i = 0; i < ATS_NET_MAX_SOCKETS; i++)
    {
        if (!s_net_ctx[i].in_use)
        {
            s_net_ctx[i].in_use = false;
            s_net_ctx[i].is_tls = false;
            s_net_ctx[i].raw_sock = INVALID_SOCKET;
            return i;
        }
    }
    return -1;
}

static ats_net_ctx_t *get_net_ctx(ats_sock_t sock)
{
    if (sock < 0 || sock >= ATS_NET_MAX_SOCKETS)
        return NULL;
    if (!s_net_ctx[sock].in_use)
        return NULL;
    return &s_net_ctx[sock];
}

/* =========================================================
 * Winsock初始化
 * ========================================================= */

static int ensure_winsock(void)
{
    if (!s_winsock_initialized)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return -1;
        s_winsock_initialized = true;
    }
    return 0;
}

/* =========================================================
 * RPC 回调函数
 * ========================================================= */

int ats_net_rpc_register_callback(ats_net_rpc_callback_t *callback)
{
    if (!callback)
        return -1;

    memcpy(&s_net_rpc_callback, callback, sizeof(ats_net_rpc_callback_t));
    return 0;
}

/* =========================================================
 * Socket 接口
 * ========================================================= */

int ats_sock_create(ats_sock_t *sock, ats_sock_family_t family, ats_sock_type_t type, ats_sock_protocol_t protocol)
{
    if (!sock)
        return -1;

    if (ensure_winsock() != 0)
        return -1;
    net_ctx_pool_init();

    int idx = alloc_net_ctx();
    if (idx < 0)
        return -1;

    ats_net_ctx_t *ctx = &s_net_ctx[idx];

    if (protocol == AST_SOCK_PROTOCOL_TLS)
    {
        /* TLS socket 延迟在 connect 时建立 */
        ctx->in_use = true;
        ctx->is_tls = true;
        mbedtls_net_init(&ctx->tls_fd);
        mbedtls_ssl_init(&ctx->ssl);
        mbedtls_ssl_config_init(&ctx->ssl_conf);
        mbedtls_entropy_init(&ctx->entropy);
        mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
        *sock = (ats_sock_t)idx;
        return 0;
    }

    /* 普通 TCP/UDP socket */
    int af = (family == ATS_SOCK_FAMILY_IPV6) ? AF_INET6 : AF_INET;
    int stype = (type == AST_SOCK_TYPE_DGRAM) ? SOCK_DGRAM : SOCK_STREAM;
    int proto = (type == AST_SOCK_TYPE_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;

    SOCKET s = socket(af, stype, proto);
    if (s == INVALID_SOCKET)
        return -1;

    ctx->in_use = true;
    ctx->is_tls = false;
    ctx->raw_sock = s;
    *sock = (ats_sock_t)idx;
    return 0;
}

int ats_sock_connect(ats_sock_t sock, const char *host, uint16_t port, unsigned int timeout_ms)
{
    ats_net_ctx_t *ctx = get_net_ctx(sock);
    if (!ctx || !host)
        return -1;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    /* ---------- TLS 连接 ---------- */
    if (ctx->is_tls)
    {
        const char *pers = "ats_tls_client";
        int ret;

        ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                    &ctx->entropy,
                                    (const unsigned char *)pers, strlen(pers));
        if (ret != 0)
            return -1;

        ret = mbedtls_net_connect(&ctx->tls_fd, host, port_str, MBEDTLS_NET_PROTO_TCP);
        if (ret != 0)
            return -1;

        ret = mbedtls_ssl_config_defaults(&ctx->ssl_conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0)
            return -1;

        /* 不验证证书（嵌入式常见做法，如需验证请传入CA证书） */
        mbedtls_ssl_conf_authmode(&ctx->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&ctx->ssl_conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

        ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->ssl_conf);
        if (ret != 0)
            return -1;

        ret = mbedtls_ssl_set_hostname(&ctx->ssl, host);
        if (ret != 0)
            return -1;

        mbedtls_ssl_set_bio(&ctx->ssl, &ctx->tls_fd,
                            mbedtls_net_send, mbedtls_net_recv,
                            mbedtls_net_recv_timeout);

        /* 设置超时 */
        if (timeout_ms > 0)
        {
            mbedtls_ssl_conf_read_timeout(&ctx->ssl_conf, timeout_ms);
        }

        /* 握手 */
        while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                return -1;
            }
        }
        return 0;
    }

    /* ---------- 普通 TCP/UDP 连接 ---------- */
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &result) != 0)
        return -1;

    /* 设置连接超时（非阻塞 + select） */
    u_long mode = 1;
    ioctlsocket(ctx->raw_sock, FIONBIO, &mode);

    connect(ctx->raw_sock, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(ctx->raw_sock, &wset);

    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

    int sel = select(0, NULL, &wset, NULL, timeout_ms > 0 ? &tv : NULL);

    /* 恢复阻塞模式 */
    mode = 0;
    ioctlsocket(ctx->raw_sock, FIONBIO, &mode);

    if (sel <= 0)
        return -1; /* 超时或错误 */

    /* 检查连接错误 */
    int err = 0;
    int len = sizeof(err);
    getsockopt(ctx->raw_sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
    return (err == 0) ? 0 : -1;
}

int ats_sock_send(ats_sock_t sock, const void *buf, unsigned int len)
{
    ats_net_ctx_t *ctx = get_net_ctx(sock);
    if (!ctx || !buf)
        return -1;

    if (ctx->is_tls)
    {
        int ret = mbedtls_ssl_write(&ctx->ssl, (const unsigned char *)buf, len);
        return (ret >= 0) ? ret : -2;
    }

    int ret = send(ctx->raw_sock, (const char *)buf, (int)len, 0);
    return (ret == SOCKET_ERROR) ? -2 : ret;
}

int ats_sock_recv(ats_sock_t sock, void *buf, unsigned int len, unsigned int timeout_ms)
{
    ats_net_ctx_t *ctx = get_net_ctx(sock);
    if (!ctx || !buf)
        return -1;

    if (ctx->is_tls)
    {
        mbedtls_ssl_conf_read_timeout(&ctx->ssl_conf, timeout_ms);
        int ret = mbedtls_ssl_read(&ctx->ssl, (unsigned char *)buf, len);
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT || ret == MBEDTLS_ERR_SSL_WANT_READ)
            return 0; /* 超时/无数据，不是错误，上层应继续重试 */
        return (ret >= 0) ? ret : -2;
    }

    /* 设置接收超时（始终设置，避免残留旧超时值） */
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(ctx->raw_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    int ret = recv(ctx->raw_sock, (char *)buf, (int)len, 0);
    return (ret == SOCKET_ERROR) ? -2 : ret;
}

int ats_sock_close(ats_sock_t sock)
{
    ats_net_ctx_t *ctx = get_net_ctx(sock);
    if (!ctx)
        return -1;

    if (ctx->is_tls)
    {
        mbedtls_ssl_close_notify(&ctx->ssl);
        mbedtls_net_free(&ctx->tls_fd);
        mbedtls_ssl_free(&ctx->ssl);
        mbedtls_ssl_config_free(&ctx->ssl_conf);
        mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
        mbedtls_entropy_free(&ctx->entropy);
    }
    else
    {
        closesocket(ctx->raw_sock);
        ctx->raw_sock = INVALID_SOCKET;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->raw_sock = INVALID_SOCKET;
    return 0;
}

/* =========================================================
 * 网络状态管理
 * ========================================================= */

int ats_net_set_mode(ats_net_mode_t mode)
{
    s_net_mode = mode;

    if (s_net_rpc_callback.net_mode_change)
        s_net_rpc_callback.net_mode_change(mode);

    return 0;
}

ats_net_mode_t ats_net_get_mode(void)
{
    return s_net_mode;
}

int ats_net_set_status(bool status)
{
    s_net_status = status;

    if (s_net_rpc_callback.net_status_change)
        s_net_rpc_callback.net_status_change(status);

    return 0;
}

bool ats_net_get_status(void)
{
    return s_net_status;
}

/* =========================================================
 * WiFi状态管理
 * ========================================================= */

int ats_net_wifi_set_module_status(bool status)
{
    s_wifi_module_status = status;

    if (s_net_rpc_callback.wifi_module_status_change)
        s_net_rpc_callback.wifi_module_status_change(status);

    return 0;
}

bool ats_net_wifi_get_module_status(void)
{
    return s_wifi_module_status;
}

int ats_net_wifi_set_ssid(const char *ssid)
{
    if (!ssid)
        return -1;
    strncpy(s_wifi_cur_ssid, ssid, sizeof(s_wifi_cur_ssid) - 1);
    s_wifi_cur_ssid[sizeof(s_wifi_cur_ssid) - 1] = '\0';
    return 0;
}

char *ats_net_wifi_get_ssid(void)
{
    return s_wifi_cur_ssid;
}

int ats_net_wifi_set_signal(int signal)
{
    s_wifi_cur_signal = signal;
    return 0;
}

int ats_net_wifi_get_signal(void)
{
    return s_wifi_cur_signal;
}

int ats_net_wifi_set_ap_list(ats_net_wifi_ap_t *ap_list, unsigned int count)
{
    /* 释放旧列表 */
    if (s_wifi_ap_list)
    {
        free(s_wifi_ap_list);
        s_wifi_ap_list = NULL;
        s_wifi_ap_count = 0;
    }

    if (!ap_list || count == 0)
        return 0;

    s_wifi_ap_list = (ats_net_wifi_ap_t *)malloc(sizeof(ats_net_wifi_ap_t) * count);
    if (!s_wifi_ap_list)
        return -1;

    memcpy(s_wifi_ap_list, ap_list, sizeof(ats_net_wifi_ap_t) * count);
    s_wifi_ap_count = count;
    return 0;
}

int ats_net_wifi_get_ap_list(ats_net_wifi_ap_t **ap_list, unsigned int *count)
{
    if (!ap_list || !count)
        return -1;
    if (s_wifi_ap_count == 0 || !s_wifi_ap_list)
    {
        *ap_list = NULL;
        *count = 0;
        return 0;
    }

    *ap_list = (ats_net_wifi_ap_t *)malloc(sizeof(ats_net_wifi_ap_t) * s_wifi_ap_count);
    if (!*ap_list)
        return -1;

    memcpy(*ap_list, s_wifi_ap_list, sizeof(ats_net_wifi_ap_t) * s_wifi_ap_count);
    *count = s_wifi_ap_count;
    return 0;
}

/* =========================================================
 * 蜂窝网络参数管理
 * ========================================================= */

int ats_net_cellular_set_mcc(int mcc)
{
    s_cellular_mcc = mcc;
    return 0;
}

int ats_net_cellular_get_mcc(void)
{
    return s_cellular_mcc;
}

int ats_net_cellular_set_mnc(int mnc)
{
    s_cellular_mnc = mnc;
    return 0;
}

int ats_net_cellular_get_mnc(void)
{
    return s_cellular_mnc;
}

int ats_net_cellular_set_lac(int lac)
{
    s_cellular_lac = lac;
    return 0;
}

int ats_net_cellular_get_lac(void)
{
    return s_cellular_lac;
}

int ats_net_cellular_set_cell_id(int cell_id)
{
    s_cellular_cell_id = cell_id;
    return 0;
}

int ats_net_cellular_get_cell_id(void)
{
    return s_cellular_cell_id;
}

int ats_net_cellular_set_signal(int signal)
{
    s_cellular_signal = signal;
    return 0;
}

int ats_net_cellular_get_signal(void)
{
    return s_cellular_signal;
}

int ats_net_cellular_set_imsi(const char *imsi)
{
    if (!imsi)
        return -1;
    strncpy(s_cellular_imsi, imsi, sizeof(s_cellular_imsi) - 1);
    s_cellular_imsi[sizeof(s_cellular_imsi) - 1] = '\0';
    return 0;
}

char *ats_net_cellular_get_imsi(void)
{
    return s_cellular_imsi;
}

int ats_net_cellular_set_imei(const char *imei)
{
    if (!imei)
        return -1;
    strncpy(s_cellular_imei, imei, sizeof(s_cellular_imei) - 1);
    s_cellular_imei[sizeof(s_cellular_imei) - 1] = '\0';
    return 0;
}

char *ats_net_cellular_get_imei(void)
{
    return s_cellular_imei;
}

int ats_net_get_connected_count(void)
{
    int count = 0;
    for (int i = 0; i < ATS_NET_MAX_SOCKETS; i++) {
        if (s_net_ctx[i].in_use)
            count++;
    }
    return count;
}
