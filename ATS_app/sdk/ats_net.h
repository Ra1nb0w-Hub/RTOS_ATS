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
 * @note 保存到一个变量中
 */
int ats_net_set_mode(ats_net_mode_t mode);

/**
 * @brief 获取网络模式(蜂窝/WiFi均使用此接口)
 * 
 * @return 网络模式
 * @note 从保存的变量中读取
 */
ats_net_mode_t ats_net_get_mode(void);

/**
 * @brief 设置网络状态(蜂窝/WiFi均使用此接口)
 * 
 * @param status 状态 true:正常 false:异常
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_set_status(bool status);

/**
 * @brief 获取网络状态(蜂窝/WiFi均使用此接口)
 * 
 * @return true:正常 false:异常
 * @note 利用Windows平台相关API查询网络的状态，优先读取变量中保存的状态为准，其次读取真实的网络状态
 */
bool ats_net_get_status(void);

/**
 * @brief 设置WiFi模块状态
 * 
 * @param status 状态 true:可用/存在 false:不可用/不存在
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_wifi_set_module_status(bool status);

/**
 * @brief 获取WiFi模块状态
 * 
 * @return true:可用/存在 false:不可用/不存在
 * @note 从保存的变量中读取
 */
bool ats_net_wifi_get_module_status(void);

/**
 * @brief 设置当前连接的WiFi SSID
 * 
 * @param ssid SSID
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_wifi_set_ssid(const char *ssid);

/**
 * @brief 获取当前连接的WiFi SSID
 * 
 * @return SSID
 * @note 从保存的变量中读取
 */
char *ats_net_wifi_get_ssid(void);

/**
 * @brief 设置当前连接的WiFi信号强度
 * 
 * @param signal 信号强度
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_wifi_set_signal(int signal);

/**
 * @brief 获取当前连接的WiFi信号强度
 * 
 * @return 信号强度
 * @note 从保存的变量中读取
 */
int ats_net_wifi_get_signal(void);

/**
 * @brief 设置WiFi周围AP列表
 * 
 * @param ap_list AP列表
 * @param count AP数量
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_wifi_set_ap_list(ats_net_wifi_ap_t *ap_list, unsigned int count);

/**
 * @brief 获取WiFi周围AP列表
 * 
 * @param ap_list AP列表，需要调用者释放内存
 * @param count AP数量
 * 
 * @return 0:成功 <0:失败
 * @note 从保存的变量中读取
 */
int ats_net_wifi_get_ap_list(ats_net_wifi_ap_t **ap_list, unsigned int *count);

/**
 * @brief 设置蜂窝网络MCC
 * 
 * @param mcc 移动国家代码
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_mcc(int mcc);

/**
 * @brief 获取蜂窝网络MCC
 * 
 * @return 移动国家代码
 * @note 从保存的变量中读取
 */
int ats_net_cellular_get_mcc(void);

/**
 * @brief 设置蜂窝网络MNC
 * 
 * @param mnc 移动设备网络代码
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_mnc(int mnc);

/**
 * @brief 获取蜂窝网络MNC
 * 
 * @return 移动设备网络代码
 * @note 从保存的变量中读取
 */
int ats_net_cellular_get_mnc(void);

/**
 * @brief 设置蜂窝网络LAC
 * 
 * @param lac 位置区域码
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_lac(int lac);

/**
 * @brief 获取蜂窝网络LAC
 * 
 * @return 位置区域码
 * @note 从保存的变量中读取
 */
int ats_net_cellular_get_lac(void);

/**
 * @brief 设置蜂窝网络CELL ID
 * 
 * @param cell_id 小区ID
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_cell_id(int cell_id);

/**
 * @brief 获取蜂窝网络CELL ID
 * 
 * @return 小区ID
 * @note 从保存的变量中读取
 */
int ats_net_cellular_get_cell_id(void);

/**
 * @brief 设置蜂窝网络Signal
 *
 * @param signal 信号值
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_signal(int signal);

/**
 * @brief 获取蜂窝网络Signal
 *
 * @return 信号值
 * @note 从保存的变量中读取
 */
int ats_net_cellular_get_signal(void);

/**
 * @brief 设置IMSI
 * 
 * @param imsi IMSI
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_imsi(const char *imsi);

/**
 * @brief 获取IMSI
 * 
 * @return IMSI
 * @note 从保存的变量中读取
 */
char *ats_net_cellular_get_imsi(void);

/**
 * @brief 设置IMEI
 * 
 * @param imei IMEI
 * 
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_net_cellular_set_imei(const char *imei);

/**
 * @brief 获取IMEI
 * 
 * @return IMEI
 * @note 从保存的变量中读取
 */
char *ats_net_cellular_get_imei(void);

/**
 * @brief 获取当前已建立连接的 socket 数量
 *
 * @return 已 in_use 的 socket 上下文数量
 */
int ats_net_get_connected_count(void);

#ifdef __cplusplus
}
#endif
