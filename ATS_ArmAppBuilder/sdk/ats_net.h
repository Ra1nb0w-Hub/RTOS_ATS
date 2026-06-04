#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int ats_sock_t;

typedef enum {
    ATS_SOCK_FAMILY_IPV4 = 0,           // IPv4
    ATS_SOCK_FAMILY_IPV6,               // IPv6
} ats_sock_family_t;

typedef enum {
    AST_SOCK_TYPE_STREAM = 0,           // Stream
    AST_SOCK_TYPE_DGRAM,                // Datagram
    AST_SOCK_TYPE_RAW,                  // Raw
} ats_sock_type_t;

typedef enum {
    AST_SOCK_PROTOCOL_TCP = 0,          // TCP协议
    AST_SOCK_PROTOCOL_UDP,              // UDP协议
    AST_SOCK_PROTOCOL_TLS,              // TLS协议
} ats_sock_protocol_t;

typedef enum {
    ATS_NET_MODE_CELLUALR = 0,          // 蜂窝网络(默认值)
    ATS_NET_MODE_WIFI,                  // WiFi网络
    ATS_NET_MODE_ETHERNET,              // 以太网网络
} ats_net_mode_t;

typedef struct {
    char ssid[64];                      // SSID
    int rssi;                           // 信号强度
    char mac[24];                       // MAC地址
} ats_net_wifi_ap_t;

typedef struct {
    void (*net_mode_change)(ats_net_mode_t mode);
    void (*net_status_change)(bool status);

    void (*wifi_module_status_change)(bool status);
} ats_net_rpc_callback_t;

/**
 * @brief 注册网络RPC回调函数
 * 
 * @param callback 回调函数指针
 * 
 * @return 0:成功 <0:失败
 */
int ats_net_rpc_register_callback(ats_net_rpc_callback_t *callback);

/**
 * @brief 创建socket
 *
 * @param sock 输出套接字句柄
 * @param family 地址族
 * @param type 类型
 * @param protocol 协议
 *
 * @return 0:成功 <0:失败
 */
int ats_sock_create(ats_sock_t *sock, ats_sock_family_t family, ats_sock_type_t type, ats_sock_protocol_t protocol);

/**
 * @brief 连接socket
 * 
 * @param sock 套接字
 * @param host 主机地址
 * @param port 端口
 * @param timeout_ms 超时时间(毫秒)
 * 
 * @return 0:成功 <0:失败
 */
int ats_sock_connect(ats_sock_t sock, const char *host, uint16_t port, unsigned int timeout_ms);

/**
 * @brief 发送数据
 * 
 * @param sock 套接字
 * @param buf 数据缓冲区
 * @param len 数据长度
 * 
 * @return 成功返回发送的字节数
 */
int ats_sock_send(ats_sock_t sock, const void *buf, unsigned int len);

/**
 * @brief 接收数据
 * 
 * @param sock 套接字
 * @param buf 数据缓冲区
 * @param len 数据长度
 * 
 * @return 成功返回接收的字节数
 */
int ats_sock_recv(ats_sock_t sock, void *buf, unsigned int len, unsigned int timeout_ms);

/**
 * @brief 关闭socket
 * 
 * @param sock 套接字
 * 
 * @return 0:成功 <0:失败
 */
int ats_sock_close(ats_sock_t sock);

/**
 * @brief 设置网络模式(蜂窝/WiFi均使用此接口)
 * 
 * @param mode 需要切换的网络模式
 * 
 * @return 0:成功 <0:失败
 */
int ats_net_set_mode(ats_net_mode_t mode);

/**
 * @brief 获取网络模式(蜂窝/WiFi均使用此接口)
 * 
 * @return 网络模式
 */
ats_net_mode_t ats_net_get_mode(void);

/**
 * @brief 获取网络状态(蜂窝/WiFi均使用此接口)
 * 
 * @return true:正常 false:异常
 */
bool ats_net_get_status(void);

/**
 * @brief 获取WiFi模块状态
 * 
 * @return true:可用/存在 false:不可用/不存在
 */
bool ats_net_wifi_get_module_status(void);

/**
 * @brief 获取当前连接的WiFi SSID
 * 
 * @return SSID
 */
char *ats_net_wifi_get_ssid(void);

/**
 * @brief 获取当前连接的WiFi信号强度
 * 
 * @return 信号强度
 */
int ats_net_wifi_get_signal(void);

/**
 * @brief 获取WiFi周围AP列表
 * 
 * @param ap_list AP列表，需要调用者释放内存
 * @param count AP数量
 * 
 * @return 0:成功 <0:失败
 */
int ats_net_wifi_get_ap_list(ats_net_wifi_ap_t **ap_list, unsigned int *count);

/**
 * @brief 获取蜂窝网络MCC
 * 
 * @return 移动国家代码
 */
int ats_net_cellular_get_mcc(void);

/**
 * @brief 获取蜂窝网络MNC
 * 
 * @return 移动设备网络代码
 */
int ats_net_cellular_get_mnc(void);

/**
 * @brief 获取蜂窝网络LAC
 * 
 * @return 位置区域码
 */
int ats_net_cellular_get_lac(void);

/**
 * @brief 获取蜂窝网络CELL ID
 * 
 * @return 小区ID
 */
int ats_net_cellular_get_cell_id(void);

/**
 * @brief 获取蜂窝网络Signal
 *
 * @return 信号值
 */
int ats_net_cellular_get_signal(void);

/**
 * @brief 获取IMSI
 * 
 * @return IMSI
 */
char *ats_net_cellular_get_imsi(void);

/**
 * @brief 获取IMEI
 * 
 * @return IMEI
 */
char *ats_net_cellular_get_imei(void);

#ifdef __cplusplus
}
#endif
