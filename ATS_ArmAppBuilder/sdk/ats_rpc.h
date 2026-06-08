#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief RPC 帧头起始字节 0 */
#define ATS_RPC_SOF0                   0xA5U
/** @brief RPC 帧头起始字节 1 */
#define ATS_RPC_SOF1                   0x5AU
/** @brief RPC 帧头固定开销(不含 payload) */
#define ATS_RPC_HEADER_SIZE            8U

/**
 * @brief RPC 通信通道
 *
 * 不同通道对应不同的 UART 物理通道，实现带宽隔离:
 * - CTRL:  控制命令通道
 * - DISPLAY: 显示/LCD 通道
 * - DATA:   数据通道(文件、网络)
 * - LOG:    日志通道
 */
typedef enum
{
    ATS_RPC_CHANNEL_CTRL = 0,           /**< 控制通道 */
    ATS_RPC_CHANNEL_DISPLAY = 1,        /**< 显示通道 */
    ATS_RPC_CHANNEL_DATA = 2,           /**< 数据通道 */
    ATS_RPC_CHANNEL_LOG = 3,            /**< 日志通道 */

    ATS_RPC_CHANNEL_COUNT               /**< 通道总数 */
} ats_rpc_channel_t;

/**
 * @brief RPC 帧类型
 */
typedef enum
{
    ATS_RPC_FRAME_TYPE_REQUEST = 1,     /**< 请求帧(需要响应) */
    ATS_RPC_FRAME_TYPE_RESPONSE = 2,    /**< 响应帧 */
    ATS_RPC_FRAME_TYPE_EVENT = 3        /**< 事件帧(单向, 无需响应) */
} ats_rpc_frame_type_t;

/**
 * @brief RPC 服务类型
 */
typedef enum
{
    ATS_RPC_SERVICE_CORE = 1,           /**< 核心服务(日志、时间、序列号等) */
    ATS_RPC_SERVICE_LCD = 2,            /**< LCD 显示服务 */
    ATS_RPC_SERVICE_PRINTER = 3,        /**< 打印机服务 */
    ATS_RPC_SERVICE_FS = 4,             /**< 文件系统服务 */
    ATS_RPC_SERVICE_NET = 5,            /**< 网络服务 */
    ATS_RPC_SERVICE_AUDIO = 6,          /**< 音频服务 */
    ATS_RPC_SERVICE_READER = 7,         /**< 读卡器服务 */

    ATS_RPC_MAX_SERVICES                /**< 最大服务数 */
} ats_rpc_service_t;

/**
 * @brief Core 服务命令码
 */
typedef enum
{
    ATS_RPC_CORE_WRITE_LOG = 1,         /**< 写日志 */
    ATS_RPC_CORE_CRASH = 2,             /**< 崩溃事件上报 */
    ATS_RPC_CORE_GET_TIMESTAMP = 3,     /**< 获取时间戳 */
    ATS_RPC_CORE_GET_SERIAL_NUMBER = 4, /**< 获取序列号 */
    ATS_RPC_CORE_GET_THREAD_INFO = 5    /**< 获取线程信息 */
} ats_rpc_core_command_t;

/**
 * @brief LCD 服务命令码
 */
typedef enum
{
    ATS_RPC_LCD_CMD_INIT = 1,               /**< 初始化 LCD */
    ATS_RPC_LCD_CMD_DRAW_RECTANGLE = 2,     /**< 绘制矩形框 */
    ATS_RPC_LCD_CMD_FILL_RECTANGLE = 3,     /**< 填充矩形区域 */
    ATS_RPC_LCD_CMD_DRAW_1BIT_BITMAP = 4,   /**< 绘制 1-bit 位图 */
    ATS_RPC_LCD_CMD_DRAW_16BIT_BITMAP = 5,  /**< 绘制 16-bit 位图(RGB565) */
    ATS_RPC_LCD_CMD_DEINIT = 6              /**< 释放 LCD 资源 */
} ats_rpc_lcd_command_t;

/**
 * @brief 打印机服务命令码
 */
typedef enum
{
    ATS_RPC_PRINTER_CMD_OPEN = 1,               /**< 打开打印机 */
    ATS_RPC_PRINTER_CMD_CLOSE = 2,              /**< 关闭打印机 */
    ATS_RPC_PRINTER_CMD_START = 3,              /**< 开始打印 */
    ATS_RPC_PRINTER_CMD_PRINT_TEXT = 4,         /**< 打印文本 */
    ATS_RPC_PRINTER_CMD_PRINT_BITMAP = 5,       /**< 打印位图 */
    ATS_RPC_PRINTER_CMD_SET_PAPER_STATUS = 6    /**< 设置纸张状态 */
} ats_rpc_printer_command_t;

/**
 * @brief 文件系统服务命令码
 */
typedef enum
{
    ATS_RPC_FS_CMD_OPEN = 1,    /**< 打开文件 */
    ATS_RPC_FS_CMD_CLOSE = 2,   /**< 关闭文件 */
    ATS_RPC_FS_CMD_READ = 3,    /**< 读取文件 */
    ATS_RPC_FS_CMD_WRITE = 4,   /**< 写入文件 */
    ATS_RPC_FS_CMD_SEEK = 5,    /**< 设置文件偏移 */
    ATS_RPC_FS_CMD_SIZE = 6,    /**< 获取文件大小 */
    ATS_RPC_FS_CMD_REMOVE = 7,  /**< 删除文件 */
    ATS_RPC_FS_CMD_EXIST = 8    /**< 检查文件是否存在 */
} ats_rpc_fs_command_t;

/**
 * @brief 网络服务命令码
 */
typedef enum
{
    ATS_RPC_NET_CMD_SOCK_CREATE = 1,                    /**< 创建 socket */
    ATS_RPC_NET_CMD_SOCK_CONNECT = 2,                   /**< 连接 socket */
    ATS_RPC_NET_CMD_SOCK_SEND = 3,                      /**< 发送数据 */
    ATS_RPC_NET_CMD_SOCK_RECV = 4,                      /**< 接收数据 */
    ATS_RPC_NET_CMD_SOCK_CLOSE = 5,                     /**< 关闭 socket */
    ATS_RPC_NET_CMD_SET_MODE = 6,                       /**< 设置网络模式 */
    ATS_RPC_NET_CMD_MODE_CHANGE = 7,                    /**< 网络模式变更事件 */
    ATS_RPC_NET_CMD_STATUS_CHANGE = 8,                  /**< 网络状态变更事件 */
    ATS_RPC_NET_CMD_WIFI_MODULE_STATUS_CHANGE = 9,      /**< WiFi 模块状态变更事件 */
    ATS_RPC_NET_CMD_WIFI_GET_SSID = 10,                 /**< 获取 WiFi SSID */
    ATS_RPC_NET_CMD_WIFI_GET_SIGNAL = 11,               /**< 获取 WiFi 信号强度 */
    ATS_RPC_NET_CMD_WIFI_GET_AP_LIST = 12,              /**< 获取 WiFi AP 列表 */
    ATS_RPC_NET_CMD_CELLULAR_GET_MCC = 13,              /**< 获取蜂窝网络 MCC */
    ATS_RPC_NET_CMD_CELLULAR_GET_MNC = 14,              /**< 获取蜂窝网络 MNC */
    ATS_RPC_NET_CMD_CELLULAR_GET_LAC = 15,              /**< 获取蜂窝网络 LAC */
    ATS_RPC_NET_CMD_CELLULAR_GET_CELL_ID = 16,          /**< 获取蜂窝网络 Cell ID */
    ATS_RPC_NET_CMD_CELLULAR_GET_SIGNAL = 17,           /**< 获取蜂窝网络信号强度 */
    ATS_RPC_NET_CMD_CELLULAR_GET_IMSI = 18,             /**< 获取蜂窝网络 IMSI */
    ATS_RPC_NET_CMD_CELLULAR_GET_IMEI = 19              /**< 获取蜂窝网络 IMEI */
} ats_rpc_net_command_t;

/**
 * @brief 音频服务命令码
 */
typedef enum
{
    ATS_RPC_AUDIO_CMD_SET_VOLUME = 1,   /**< 设置音量 */
    ATS_RPC_AUDIO_CMD_GET_VOLUME = 2,   /**< 获取音量 */
    ATS_RPC_AUDIO_CMD_PLAY_FILE = 3     /**< 播放音频文件 */
} ats_rpc_audio_command_t;

/**
 * @brief 读卡器服务命令码
 */
typedef enum
{
    ATS_RPC_READER_CMD_INIT = 1,                    /**< 初始化读卡器 */
    ATS_RPC_READER_CMD_OPEN = 2,                    /**< 打开读卡器 */
    ATS_RPC_READER_CMD_CLOSE = 3,                   /**< 关闭读卡器 */
    ATS_RPC_READER_CMD_POLL = 4,                    /**< 轮询检测卡片 */
    ATS_RPC_READER_CMD_CANCEL = 5,                  /**< 取消轮询 */
    ATS_RPC_READER_CMD_ICC_POWER_ON = 6,            /**< ICC 上电获取 ATR */
    ATS_RPC_READER_CMD_ICC_POWER_OFF = 7,           /**< ICC 下电 */
    ATS_RPC_READER_CMD_ICC_TRANSCEIVE_APDU = 8,     /**< ICC APDU 透传 */
    ATS_RPC_READER_CMD_PICC_ACTIVATE = 9,           /**< PICC 激活获取 ATS */
    ATS_RPC_READER_CMD_PICC_DEACTIVATE = 10,        /**< PICC 去激活 */
    ATS_RPC_READER_CMD_PICC_TRANSCEIVE_APDU = 11,   /**< PICC APDU 透传 */
    ATS_RPC_READER_CMD_GET_LAST_HW_ERROR = 12       /**< 获取最后一次硬件错误 */
} ats_rpc_reader_command_t;

/**
 * @brief 位图编码方式
 */
typedef enum
{
    ATS_RPC_BITMAP_ENCODING_RAW = 0,    /**< 原始格式(未编码) */
    ATS_RPC_BITMAP_ENCODING_RLE8 = 1,   /**< RLE8 编码 */
    ATS_RPC_BITMAP_ENCODING_RLE16 = 2   /**< RLE16 编码 */
} ats_rpc_bitmap_encoding_t;

/**
 * @brief RPC 帧结构
 */
typedef struct
{
    uint8_t frame_type;         /**< 帧类型 @see ats_rpc_frame_type_t */
    uint8_t service;            /**< 服务号 @see ats_rpc_service_t */
    uint8_t command;            /**< 命令号 */
    uint8_t request_id;         /**< 请求 ID(用于匹配请求与响应) */
    uint16_t payload_length;    /**< 载荷长度(字节) */
    uint8_t *payload;           /**< 载荷数据指针 */
} ats_rpc_frame_t;

/**
 * @brief RPC 服务处理函数类型
 *
 * @param[in] frame 收到的 RPC 帧
 *
 * @return 0:成功 <0:失败
 */
typedef int (*ats_rpc_handler_t)(const ats_rpc_frame_t *frame);

/**
 * @brief 向缓冲区写入 16 位小端序整数
 *
 * @param[out] buffer 目标缓冲区(至少 2 字节)
 * @param[in] value 要写入的值
 */
void ats_rpc_write_u16_le(uint8_t *buffer, uint16_t value);

/**
 * @brief 从缓冲区读取 16 位小端序整数
 *
 * @param[in] buffer 源缓冲区
 *
 * @return 读取的 16 位值
 */
uint16_t ats_rpc_read_u16_le(const uint8_t *buffer);

/**
 * @brief 向缓冲区写入 32 位小端序整数
 *
 * @param[out] buffer 目标缓冲区(至少 4 字节)
 * @param[in] value 要写入的值
 */
void ats_rpc_write_u32_le(uint8_t *buffer, uint32_t value);

/**
 * @brief 从缓冲区读取 32 位小端序整数
 *
 * @param[in] buffer 源缓冲区
 *
 * @return 读取的 32 位值
 */
uint32_t ats_rpc_read_u32_le(const uint8_t *buffer);

/**
 * @brief 发送单向 RPC 事件帧(无需响应)
 *
 * @param[in] service 服务号
 * @param[in] command 命令号
 * @param[in] payload 事件载荷数据
 * @param[in] payload_length 载荷长度(字节)
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_event(uint8_t service, uint8_t command, const uint8_t *payload, uint16_t payload_length);

/**
 * @brief 发送崩溃事件(Panic上报)
 *
 * @note 可在中断上下文中调用, 不依赖互斥锁
 *
 * @param[in] pc 崩溃时的 PC 寄存器值
 * @param[in] lr 崩溃时的 LR 寄存器值
 */
void ats_rpc_event_for_crash(uint32_t pc, uint32_t lr);

/**
 * @brief 发送 RPC 响应帧
 *
 * @param[in] service 服务号
 * @param[in] command 命令号
 * @param[in] request_id 匹配的请求 ID
 * @param[in] payload 响应载荷数据
 * @param[in] payload_length 载荷长度(字节)
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_response(uint8_t service, uint8_t command, uint8_t request_id, const uint8_t *payload, uint16_t payload_length);

/**
 * @brief 发送 RPC 请求并等待同步响应
 *
 * @param[in] service 服务号
 * @param[in] command 命令号
 * @param[in] request_payload 请求载荷数据
 * @param[in] request_length 请求载荷长度(字节)
 * @param[out] response_payload 响应载荷缓冲区
 * @param[in,out] response_length 输入缓冲区大小, 输出实际响应长度
 * @param[in] timeout_ms 超时时间(毫秒)
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_request(uint8_t service, uint8_t command, const uint8_t *request_payload, uint16_t request_length, uint8_t *response_payload, uint16_t *response_length, uint32_t timeout_ms);

/**
 * @brief 初始化 RPC 子系统
 *
 * 初始化 UART 通道、互斥锁、信号量等资源。
 * 在系统启动时调用, 且仅调用一次。
 */
void ats_rpc_init(void);

/**
 * @brief 注册指定服务的 RPC 处理函数
 *
 * @param[in] service 服务号 @see ats_rpc_service_t
 * @param[in] handler 服务处理函数
 */
void ats_rpc_register_service(uint8_t service, const ats_rpc_handler_t handler);

/**
 * @brief 分发 RPC 帧到已注册的服务处理函数
 *
 * @param[in] frame RPC 帧指针
 */
void ats_rpc_dispatch(const ats_rpc_frame_t *frame);

/**
 * @brief Core 服务 RPC 处理函数
 *
 * 处理日志写入、崩溃上报、时间戳获取、序列号获取、线程信息等请求。
 *
 * @param[in] frame RPC 帧指针
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_core_handler(const ats_rpc_frame_t *frame);

/**
 * @brief 打印机服务 RPC 处理函数
 *
 * 处理打印机打开/关闭/开始打印/打印文本/打印位图/纸张状态设置等请求。
 *
 * @param[in] frame RPC 帧指针
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_printer_handler(const ats_rpc_frame_t *frame);

/**
 * @brief 网络服务 RPC 处理函数
 *
 * 处理网络模式变更、网络状态变更、WiFi 模块状态变更等回调事件。
 *
 * @param[in] frame RPC 帧指针
 *
 * @return 0:成功 <0:失败
 */
int ats_rpc_net_handler(const ats_rpc_frame_t *frame);

#ifdef __cplusplus
}
#endif
