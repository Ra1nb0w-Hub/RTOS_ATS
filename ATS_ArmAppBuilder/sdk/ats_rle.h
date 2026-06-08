#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 计算 8 位字节流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始字节数
 *
 * @return 最坏情况下的编码长度(字节)
 */
uint16_t ats_rle8_worst_case_size(uint16_t source_length);

/**
 * @brief 对 8 位字节流执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块, 长度 = control + 1, 后跟原始字节
 * - control bit7=1: 重复块, 长度 = (control & 0x7F) + 1, 后跟 1 个重复字节
 *
 * @param[in] source 原始字节流
 * @param[in] source_length 原始字节数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 成功返回编码后字节数, 失败返回 0
 */
uint16_t ats_rle8_encode(const uint8_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity);

/**
 * @brief 计算 16 位像素流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始像素数
 *
 * @return 最坏情况下的编码长度(字节)
 */
uint16_t ats_rle16_worst_case_size(uint16_t source_length);

/**
 * @brief 对 16 位像素流(如 RGB565)执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块, 长度 = control + 1, 后跟对应数量的 RGB565 像素(小端序)
 * - control bit7=1: 重复块, 长度 = (control & 0x7F) + 1, 后跟 1 个重复 RGB565 像素(小端序)
 *
 * @param[in] source 原始像素流
 * @param[in] source_length 原始像素数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 成功返回编码后字节数, 失败返回 0
 */
uint16_t ats_rle16_encode(const uint16_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity);

#ifdef __cplusplus
}
#endif