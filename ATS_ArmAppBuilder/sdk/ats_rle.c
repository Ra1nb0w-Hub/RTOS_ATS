#include "ats_rle.h"
#include "ats_rpc.h"
#include <string.h>

/**
 * @brief 统计连续重复字节数量
 *
 * @param[in] source 原始字节流
 * @param[in] offset 起始偏移
 * @param[in] source_length 原始总长度
 *
 * @return 连续重复数量，范围 1..128
 */
static uint16_t count_repeated_bytes(const uint8_t *source, uint16_t offset, uint16_t source_length)
{
    uint16_t repeat_count = 1U;

    while (((uint16_t)(offset + repeat_count) < source_length) &&
           (repeat_count < 128U) &&
           (source[offset + repeat_count] == source[offset]))
    {
        ++repeat_count;
    }

    return repeat_count;
}

/**
 * @brief 统计连续重复 16 位像素数量
 *
 * @param[in] source 原始像素流
 * @param[in] offset 起始偏移
 * @param[in] pixel_count 原始像素总数
 *
 * @return 连续重复数量，范围 1..128
 */
static uint16_t count_repeated_words(const uint16_t *source, uint16_t offset, uint16_t pixel_count)
{
    uint16_t repeat_count = 1U;

    while (((uint16_t)(offset + repeat_count) < pixel_count) &&
           (repeat_count < 128U) &&
           (source[offset + repeat_count] == source[offset]))
    {
        ++repeat_count;
    }

    return repeat_count;
}

/**
 * @brief 计算 8 位像素流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始字节数
 *
 * @return 最坏情况下的编码长度（字节）
 */
uint16_t ats_rle8_worst_case_size(uint16_t source_length)
{
    return (uint16_t)(source_length + (source_length + 2U) / 3U);
}

/**
 * @brief 对字节流执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块，长度 = control + 1，后跟原始字节
 * - control bit7=1: 重复块，长度 = (control & 0x7F) + 1，后跟 1 个重复字节
 *
 * @param[in] source 原始字节流
 * @param[in] source_length 原始字节数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 编码后字节数，失败返回 0
 */
uint16_t ats_rle8_encode(const uint8_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity)
{
    uint16_t source_offset = 0U;
    uint16_t destination_offset = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while (source_offset < source_length)
    {
        const uint16_t repeat_count = count_repeated_bytes(source, source_offset, source_length);
        if (repeat_count >= 2U)
        {
            if ((uint16_t)(destination_offset + 2U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(0x80U | (uint8_t)(repeat_count - 1U));
            destination[destination_offset++] = source[source_offset];
            source_offset = (uint16_t)(source_offset + repeat_count);
            continue;
        }

        {
            const uint16_t literal_start = source_offset;
            uint16_t literal_count = 1U;

            ++source_offset;
            while ((source_offset < source_length) && (literal_count < 128U))
            {
                if (count_repeated_bytes(source, source_offset, source_length) >= 2U)
                {
                    break;
                }

                ++source_offset;
                ++literal_count;
            }

            if ((uint16_t)(destination_offset + 1U + literal_count) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(literal_count - 1U);
            (void)memcpy(&destination[destination_offset], &source[literal_start], literal_count);
            destination_offset = (uint16_t)(destination_offset + literal_count);
        }
    }

    return destination_offset;
}

/**
 * @brief 计算 16 位像素流 RLE 编码后的最坏情况长度
 *
 * @param[in] source_length 原始字节数
 *
 * @return 最坏情况下的编码长度（字节）
 */
uint16_t ats_rle16_worst_case_size(uint16_t source_length)
{
    return (uint16_t)((source_length * 2U) + (source_length + 2U) / 3U);
}

/**
 * @brief 对 16 位像素流执行 RLE 编码
 *
 * 编码格式:
 * - control bit7=0: 字面量块，长度 = control + 1，后跟对应数量的 RGB565 像素
 * - control bit7=1: 重复块，长度 = (control & 0x7F) + 1，后跟 1 个重复 RGB565 像素
 *
 * @param[in] source 原始像素流
 * @param[in] source_length 原始字节数数
 * @param[out] destination 编码输出缓冲区
 * @param[in] destination_capacity 输出缓冲区容量
 *
 * @return 编码后字节数，失败返回 0
 */
uint16_t ats_rle16_encode(const uint16_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity)
{
    uint16_t source_offset = 0U;
    uint16_t destination_offset = 0U;

    if ((source == NULL) || (destination == NULL))
    {
        return 0U;
    }

    while (source_offset < source_length)
    {
        const uint16_t repeat_count = count_repeated_words(source, source_offset, source_length);
        if (repeat_count >= 2U)
        {
            if ((uint16_t)(destination_offset + 3U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(0x80U | (uint8_t)(repeat_count - 1U));
            ats_rpc_write_u16_le(&destination[destination_offset], source[source_offset]);
            destination_offset = (uint16_t)(destination_offset + 2U);
            source_offset = (uint16_t)(source_offset + repeat_count);
            continue;
        }

        {
            const uint16_t literal_start = source_offset;
            uint16_t literal_count = 1U;

            ++source_offset;
            while ((source_offset < source_length) && (literal_count < 128U))
            {
                if (count_repeated_words(source, source_offset, source_length) >= 2U)
                {
                    break;
                }

                ++source_offset;
                ++literal_count;
            }

            if ((uint16_t)(destination_offset + 1U + literal_count * 2U) > destination_capacity)
            {
                return 0U;
            }

            destination[destination_offset++] = (uint8_t)(literal_count - 1U);
            {
                uint16_t literal_index;
                for (literal_index = 0U; literal_index < literal_count; ++literal_index)
                {
                    ats_rpc_write_u16_le(&destination[destination_offset],
                                 source[(uint16_t)(literal_start + literal_index)]);
                    destination_offset = (uint16_t)(destination_offset + 2U);
                }
            }
        }
    }

    return destination_offset;
}