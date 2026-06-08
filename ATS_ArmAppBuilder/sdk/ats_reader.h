#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "emv_lib/include/emv_api.h"

/**
 * @brief 初始化读卡器
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_init(void);

/**
 * @brief 打开读卡器
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_open(void);

/**
 * @brief 关闭读卡器
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_close(void);

/**
 * @brief 轮询检测卡片
 *
 * @param[out] card_interface 输出检测到的卡片接口类型
 * @param[in] timeout_ms 轮询超时时间(毫秒), 最小 1000ms
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_poll(EMVInterfaceType *card_interface, unsigned int timeout_ms);

/**
 * @brief 取消轮询操作
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_cancel(void);

/**
 * @brief 接触式卡(ICC)上电, 获取 ATR
 *
 * @param[out] atr ATR 数据缓冲区(可为 NULL, 此时仅获取长度)
 * @param[in,out] atr_len 输入缓冲区大小, 输出实际 ATR 长度
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_icc_power_on(unsigned char *atr, size_t *atr_len);

/**
 * @brief 接触式卡(ICC)下电
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_icc_power_off(void);

/**
 * @brief 接触式卡(ICC) APDU 透传
 *
 * @param[in] command 发送的 APDU 命令
 * @param[in] command_len 命令长度
 * @param[out] response 响应数据缓冲区(可为 NULL, 此时仅获取长度)
 * @param[in,out] response_len 输入缓冲区大小, 输出实际响应长度
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_icc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

/**
 * @brief 非接触式卡(PICC)激活, 获取 ATS
 *
 * @param[out] ats ATS 数据缓冲区(可为 NULL, 此时仅获取长度)
 * @param[in,out] ats_len 输入缓冲区大小, 输出实际 ATS 长度
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_picc_activate(unsigned char *ats, size_t *ats_len);

/**
 * @brief 非接触式卡(PICC)去激活
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_picc_deactivate(void);

/**
 * @brief 非接触式卡(PICC) APDU 透传
 *
 * @param[in] command 发送的 APDU 命令
 * @param[in] command_len 命令长度
 * @param[out] response 响应数据缓冲区(可为 NULL, 此时仅获取长度)
 * @param[in,out] response_len 输入缓冲区大小, 输出实际响应长度
 *
 * @return 0:成功 <0:失败
 */
int ats_reader_picc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

/**
 * @brief 获取最后一次硬件错误码
 *
 * @return 错误码, 0 表示无错误
 */
int ats_reader_get_last_hw_error(void);

#ifdef __cplusplus
}
#endif
