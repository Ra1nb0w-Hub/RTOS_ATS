#include "emv_internal.h"

#include <string.h>
#include <stdlib.h>

// TLV数据
typedef struct EMVTlvData {
    uint16_t tag;
    size_t length;
    unsigned char *value;
} EMVTlvData;

/**
 * @brief 构建 TLV 标签值对。
 * 
 * @param usTag 标签。
 * @param pucValue 值缓冲区指针。
 * @param uiLength 值长度。
 * @param pucOutData 输出缓冲区指针。
 * @param puiOutDataLen 输出缓冲区长度指针。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_build(unsigned short usTag, unsigned char *pucValue, size_t uiLength, unsigned char *pucOutData, size_t *puiOutDataLen)
{
    size_t uiOffset = 0;

    if (pucValue == NULL)
    {
        EmvLog("Parameter `pucValue` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (pucOutData == NULL)
    {
        EmvLog("Parameter `pucOutData` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (puiOutDataLen == NULL)
    {
        EmvLog("Parameter `puiOutDataLen` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (uiLength > *puiOutDataLen)
    {
        EmvLog("Parameter `uiLength` is %d, but max is %d", uiLength, *puiOutDataLen);
        return EMV_ERR_INVALID_PARAM;
    }

    if (usTag >> 8)
        pucOutData[uiOffset++] = usTag >> 8;
    pucOutData[uiOffset++] = usTag & 0xFF;

    if (uiLength > 0x80U)
    {
        unsigned int uiCount = uiLength / 0x80U;

        if (uiOffset + uiCount + 1 > *puiOutDataLen)
        {
            EmvLog("Parameter `pucOutData` buff size too small(%d bytes)", *puiOutDataLen);
            return EMV_ERR_INVALID_PARAM;
        }

        pucOutData[uiOffset++] = 0x80U | uiCount;
        for (int i = (uiCount - 1); i >= 0; i--)
            pucOutData[uiOffset++] = (unsigned char)(uiLength >> (i * 8)) & 0xFF;
    }
    else
    {
        if (uiOffset + 1 > *puiOutDataLen)
        {
            EmvLog("Parameter `pucOutData` buff size too small(%d bytes)", *puiOutDataLen);
            return EMV_ERR_INVALID_PARAM;
        }

        pucOutData[uiOffset++] = (unsigned char)uiLength;
    }

    if (uiLength > 0)
    {
        memcpy(pucOutData + uiOffset, pucValue, uiLength);
        uiOffset += uiLength;
    }

    *puiOutDataLen = uiOffset;
    return EMV_OK;
}

/**
 * @brief 解析 TLV 标签。
 *
 * @param data TLV 原始数据。
 * @param length 数据长度。
 * @param offset 当前读取偏移，输出时更新为标签后的偏移。
 * @param tag_out 输出标签值。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_parse_tag(const unsigned char *data, size_t length, size_t *offset, uint16_t *tag_out)
{
    uint16_t tag = 0;

    if (data == NULL)
    {
        EmvLog("Parameter `data` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (length == 0)
    {
        EmvLog("Parameter `length` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (offset == NULL)
    {
        EmvLog("Parameter `offset` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (*offset >= length)
        return EMV_ERR_INVALID_PARAM;

    if (tag_out == NULL)
    {
        EmvLog("Parameter `tag_out` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    // 读取标签字节
    tag = data[*offset];
    (*offset)++;

    // 如果标签字节包含0x1F，说明标签为扩展标签，需要读取下一个字节
    if ((tag & 0x1FU) == 0x1FU)
    {
        if (*offset >= length)
        {
            EmvLog("Parameter `offset` is %d, but max is %d", *offset, length);
            return EMV_ERR_TLV_TAG_INVALID;
        }

        tag = (tag << 8) | data[*offset];
        (*offset)++;
    }

    *tag_out = tag;
    return EMV_OK;
}

/**
 * @brief 解析 TLV 长度域。
 *
 * @param data TLV 原始数据。
 * @param length 数据长度。
 * @param offset 当前读取偏移，输出时更新为值域起始偏移。
 * @param value_length_out 输出值域长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_parse_length(const unsigned char *data, size_t length, size_t *offset, size_t *value_length_out)
{
    unsigned char first_len = 0;
    size_t value_len = 0;
    unsigned char bytes = 0;

    if (data == NULL)
    {
        EmvLog("Parameter `data` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (length == 0)
    {
        EmvLog("Parameter `length` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (offset == NULL)
    {
        EmvLog("Parameter `offset` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (*offset >= length)
        return EMV_ERR_INVALID_PARAM;

    if (value_length_out == NULL)
    {
        EmvLog("Parameter `value_length_out` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    // 读取第一个字节
    first_len = data[*offset];
    (*offset)++;

    // 如果第一个字节不含0x80，直接返回该字节值作为长度域
    if ((first_len & 0x80U) == 0)
    {
        *value_length_out = first_len;
        return EMV_OK;
    }

    // 获取长度域字节数
    bytes = (unsigned char)(first_len & 0x7FU);
    if (bytes == 0 || bytes > sizeof(size_t))
    {
        EmvLog("Get length bytes is invalid(%d)", bytes);
        return EMV_ERR_TLV_LENGTH_INVALID;
    }

    // 检查长度域字节数是否超出数据范围
    if (*offset + bytes >= length)
    {
        EmvLog("Parameter `offset` is %d, but max is %d", *offset + bytes, length);
        return EMV_ERR_TLV_LENGTH_INVALID;
    }

    // 计算值域长度
    for (unsigned char i = 0; i < bytes; ++i)
        value_len = (value_len << 8) | data[*offset + i];

    *offset += bytes;
    *value_length_out = value_len;
    return EMV_OK;
}

/**
 * @brief 解析 TLV 数据并存储到容器中。
 * 
 * @param data TLV 数据缓冲区。
 * @param length TLV 数据长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_parse_data_with_save(const unsigned char *data, size_t length)
{
    int ret = EMV_OK;
    size_t offset = 0;
    uint16_t tag = 0;

    if (data == NULL)
    {
        EmvLog("Parameter `data` is null");
        return EMV_ERR_INVALID_PARAM;
    }

    if (length == 0)
    {
        EmvLog("Parameter `length` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    while (offset < length)
    {
        size_t value_len = 0;

        ret = emv_tlv_parse_tag(data, length, &offset, &tag);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_tag failed(%d), offset = %u", ret, offset);
            return ret;
        }

        ret = emv_tlv_parse_length(data, length, &offset, &value_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_length failed(%d), offset = %u", ret, offset);
            return ret;
        }

        ret = emv_tlv_set(tag, data + offset, value_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_set failed(%d), tag = %X, len = %u", ret, tag, value_len);
            return ret;
        }

        offset += value_len;
    }

    return EMV_OK;
}

/**
 * @brief 在 TLV 数据中递归查找首个目标标签。
 *
 * @param data TLV 原始数据。
 * @param length 数据长度。
 * @param target_tag 目标标签。
 * @param depth 递归深度限制。
 * @param value_ptr_out 输出值域指针。
 * @param value_length_out 输出值域长度。
 *
 * @return true:找到 false:未找到
 */
bool emv_tlv_find_tag(const unsigned char *data, size_t length, uint16_t target_tag, uint8_t depth, const unsigned char **value_ptr_out, size_t *value_length_out)
{
    size_t offset = 0;

    if (data == NULL)
    {
        EmvLog("Parameter `data` is null");
        return false;
    }

    if (length == 0)
    {
        EmvLog("Parameter `length` is 0");
        return false;
    }

    if (target_tag == 0)
    {
        EmvLog("Parameter `target_tag` is 0");
        return false;
    }

    if (depth == 0)
    {
        EmvLog("Parameter `depth` is 0");
        return false;
    }

    while (offset < length)
    {
        unsigned short tag = 0;
        size_t value_len = 0;
        const unsigned char *value_ptr = NULL;
        unsigned char first_tag_byte = 0;
        int is_constructed = 0;

        if (emv_tlv_parse_tag(data, length, &offset, &tag) != EMV_OK)
            return false;

        if (emv_tlv_parse_length(data, length, &offset, &value_len) != EMV_OK)
            return false;

        if (value_len > (length - offset))
            return false;

        value_ptr = data + offset;
        if (tag == target_tag)
        {
            if (value_ptr_out)
                *value_ptr_out = value_ptr;
            if (value_length_out)
                *value_length_out = value_len;
            return true;
        }

        first_tag_byte = (unsigned char)((tag > 0xFFU) ? (tag >> 8) : tag);
        is_constructed = ((first_tag_byte & 0x20U) != 0U) ? 1 : 0;
        if (is_constructed && depth > 1 && emv_tlv_find_tag(value_ptr, value_len, target_tag, depth - 1, value_ptr_out, value_length_out))
            return true;

        offset += value_len;
    }

    return false;
}

/**
 * @brief 向 TLV 存储容器写入或更新一个标签值。
 *
 * @param tag EMV标签值。
 * @param value 标签值缓冲区。
 * @param length 标签值长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_set(uint16_t tag, const unsigned char *value, size_t length)
{
    int ret = EMV_OK;
    EMVTlvData *tlv = NULL;

    if (tag == 0)
    {
        EmvLog("Parameter `tag` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (length > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `length` is %d, but max is %d", length, EMV_MAX_TAG_VALUE_LEN);
        return EMV_ERR_BUFFER_TOO_SMALL;
    }

    // 检查是否存在相同Tag，存在执行更新操作，否则执行新增操作
    for (size_t i = 0; i < g_emv_session.tlv.count; ++i)
    {
        tlv = (EMVTlvData *)g_emv_session.tlv.items[i];
        if (tlv && tlv->tag == tag)
        {
            // EmvLog("Update tag `%X`, length %d", tag, length);
            // if (length <= 32)
            //     EmvHexLog("Value", value, length);

            if (length > tlv->length)
            {
                unsigned char *new_value = NULL;
                    
                new_value = (unsigned char *)malloc(length);
                if (new_value == NULL)
                {
                    EmvLog("malloc failed(%d bytes)", length);
                    return EMV_ERR_NO_MEMORY;
                }
                memset(new_value, 0, length);

                free(tlv->value);
                tlv->value = new_value;
            }

            tlv->length = length;
            if (length > 0)
                memcpy(tlv->value, value, length);

            return EMV_OK;
        }
    }

    ret = emv_tools_container_init(&g_emv_session.tlv, EMV_DEFAULT_TAG_ITEMS_CAPACITY);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_init failed(%d)", ret);
        return ret;
    }

    tlv = (EMVTlvData *)malloc(sizeof(EMVTlvData));
    if (tlv == NULL)
    {
        EmvLog("malloc failed(%d bytes)", sizeof(EMVTlvData));
        return EMV_ERR_NO_MEMORY;
    }

    memset(tlv, 0, sizeof(EMVTlvData));
    tlv->tag = tag;
    tlv->length = length;
    if (length > 0)
    {
        tlv->value = (unsigned char *)malloc(length);
        if (tlv->value == NULL)
        {
            EmvLog("malloc failed(%d bytes)", length);
            return EMV_ERR_NO_MEMORY;
        }
        memset(tlv->value, 0, length);

        if (value)
            memcpy(tlv->value, value, length);
    }

    ret = emv_tools_container_add(&g_emv_session.tlv, tlv);
    if (ret != EMV_OK)
    {
        if (tlv)
            free(tlv);

        EmvLog("emv_tools_container_add failed(%d)", ret);
        return ret;
    }

    // EmvLog("Add tag `%X`, length %d", tag, length);
    // if (length <= 32)
    //     EmvHexLog("Value", value, length);
    return EMV_OK;
}

/**
 * @brief 从 TLV 存储容器读取指定标签值。
 *
 * @param tag EMV标签值。
 * @param value 输出缓冲区。
 * @param length 输入时为缓冲区大小，输出时为实际数据长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tlv_get(uint16_t tag, unsigned char *value, size_t *length)
{
    EMVTlvData *tlv = NULL;
    size_t value_capacity = 0;

    if (length)
    {
        value_capacity = *length;
        *length = 0;
    }

    if (!value)
    {
        EmvLog("Parameter `value` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    for (size_t i = 0; i < g_emv_session.tlv.count; ++i)
    {
        tlv = (EMVTlvData *)g_emv_session.tlv.items[i];
        if (tlv && tlv->tag == tag)
        {
            // EmvLog("Get tag `%X`, length %d", tag, tlv->length);
            // if (tlv->length <= 32)
            //     EmvHexLog("Value", tlv->value, tlv->length);

            if (value_capacity && value_capacity < tlv->length)
            {
                EmvLog("[Tag: %X] Parameter `value` capacity is %d, but `value` length is %d", tag, value_capacity, tlv->length);
                return EMV_ERR_BUFFER_TOO_SMALL;
            }

            if (tlv->length > 0)
                memcpy(value, tlv->value, tlv->length);

            if (length)
                *length = tlv->length;
            return EMV_OK;
        }
    }

    return EMV_ERR_NOT_FOUND;
}

/**
 * @brief 判断 TLV 存储容器中是否存在指定标签。
 * 
 * @param tag EMV标签值。
 *
 * @return true:表示存在 false:表示不存在
 */
bool emv_tlv_exists(uint16_t tag)
{
    EMVTlvData *tlv = NULL;

    for (size_t i = 0; i < g_emv_session.tlv.count; ++i)
    {
        tlv = (EMVTlvData *)g_emv_session.tlv.items[i];
        if (tlv && tlv->tag == tag)
            return true;
    }

    return false;
}

/**
 * @brief 清空 TLV 存储容器。
 */
void emv_tlv_clear(void)
{
    EMVTlvData *tlv = NULL;

    for (size_t i = 0; i < g_emv_session.tlv.count; ++i)
    {
        tlv = (EMVTlvData *)g_emv_session.tlv.items[i];
        if (tlv == NULL)
            continue;

        if (tlv->value && tlv->length)
            free(tlv->value);

        tlv->value = NULL;
        tlv->length = 0;
        tlv->tag = 0;

        free(g_emv_session.tlv.items[i]);
        g_emv_session.tlv.items[i] = NULL;
    }

    emv_tools_container_clear(&g_emv_session.tlv);
}
