#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>
#include <stdlib.h>

static EMVContainer g_emv_app_capks_container = {0};

/**
 * @brief 验证 CAPK 条目的字段合法性。
 *
 * @param capk 待校验的 CAPK 条目。
 *
 * @return EMV_OK 表示合法，否则返回错误码。
 */
static int _validate_capk_item(const EMVCapk *capk)
{
    if (!capk)
    {
        EmvLog("Parameter `capk` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (capk->modulus_len == 0 || capk->modulus_len > EMV_MAX_MODULUS_LEN)
    {
        EmvLog("Parameter `capk` modulus_len is invalid(%d)", capk->modulus_len);
        return EMV_ERR_INVALID_PARAM;
    }

    if (capk->exponent_len == 0 || capk->exponent_len > EMV_MAX_EXPONENT_LEN)
    {
        EmvLog("Parameter `capk` exponent_len is invalid(%d)", capk->exponent_len);
        return EMV_ERR_INVALID_PARAM;
    }

    return EMV_OK;
}

/**
 * @brief 清空 CAPK 表。
 */
void emv_capk_clear(void)
{
    emv_tools_container_clear(&g_emv_app_capks_container);
}

/**
 * @brief 设置 CAPK 表。
 *
 * 该接口会校验并拷贝整表 CAPK 数据，用于后续离线认证阶段查询。
 * @param new_capk CAPK 表对象。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_capk_import(const EMVCapk *new_capk)
{
    int ret = EMV_OK;
    EMVCapk *temp_capk = NULL;

    if (!new_capk)
    {
        EmvLog("Parameter `new_capk` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = _validate_capk_item(new_capk);
    if (ret != EMV_OK)
    {
        EmvLog("_validate_capk_item failed(%d)", ret);
        return ret;
    }

    ret = emv_tools_container_init(&g_emv_app_capks_container, EMV_DEFAULT_CAPK_CAPACITY);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_init failed(%d)", ret);
        return ret;
    }

    temp_capk = (EMVCapk *)g_emv_terminal.malloc(sizeof(EMVCapk));
    if (!temp_capk)
    {
        EmvLog("malloc failed(%d bytes)", sizeof(EMVCapk));
        return EMV_ERR_NO_MEMORY;
    }

    memcpy(temp_capk, new_capk, sizeof(EMVCapk));
    ret = emv_tools_container_add(&g_emv_app_capks_container, temp_capk);
    if (ret != EMV_OK)
    {
        if (temp_capk)
            g_emv_terminal.free(temp_capk);

        EmvLog("emv_tools_container_add failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 从 TLV 数据导入一条 CAPK 配置。
 * 
 * @param tlv_data CAPK TLV 缓冲区。
 * @param tlv_length TLV 缓冲区长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_capk_import_from_tlv(const unsigned char *tlv_data, unsigned int tlv_length)
{
    int ret = EMV_OK;
    size_t offset = 0;
    EMVCapk capk;
    unsigned char state = 0x00;

    if (tlv_data == NULL)
    {
        EmvLog("Parameter `tlv_data` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (tlv_length == 0)
    {
        EmvLog("Parameter `tlv_length` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    memset(&capk, 0, sizeof(capk));
    while (offset < tlv_length)
    {
        unsigned short tag = 0;
        size_t value_length = 0;
        const unsigned char *value_ptr = NULL;

        ret = emv_tlv_parse_tag(tlv_data, tlv_length, &offset, &tag);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_tag failed(%d)", ret);
            return EMV_ERR_TLV_TAG_INVALID;
        }

        ret = emv_tlv_parse_length(tlv_data, tlv_length, &offset, &value_length);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_length failed(%d)", ret);
            return EMV_ERR_TLV_LENGTH_INVALID;
        }

        if (offset + value_length > tlv_length)
        {
            EmvLog("The value length is inconsistent(%d, %d)", offset + value_length, tlv_length);
            return EMV_ERR_BAD_DATA;
        }

        // EmvLog("Tag: 0x%X, Length: %d", tag, value_length);
        value_ptr = tlv_data + offset;
        switch (tag)
        {
        case 0x9F06:// RID
            if (value_length != sizeof(capk.rid))
            {
                EmvLog("RID length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }

            memcpy(capk.rid, value_ptr, sizeof(capk.rid));
            state |= 0x01;
            break;
        case 0x9F22:// CA公钥索引
            if (value_length != 1)
            {
                EmvLog("Key ID length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }

            capk.key_id = value_ptr[0];
            state |= 0x02;
            break;
        case 0xDF02:// CA公钥模数
            if (value_length == 0 || value_length > EMV_MAX_MODULUS_LEN)
            {
                EmvLog("Modulus length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }

            capk.modulus_len = (unsigned char)value_length;
            memcpy(capk.modulus, value_ptr, value_length);
            state |= 0x04;
            break;
        case 0xDF04:// CA公钥指数
            if (value_length == 0 || value_length > EMV_MAX_EXPONENT_LEN)
            {
                EmvLog("Exponent length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }

            capk.exponent_len = (unsigned char)value_length;
            memcpy(capk.exponent, value_ptr, value_length);
            state |= 0x08;
            break;
        default:
            break;
        }

        offset += value_length;
    }

    if (state != 0x0F)
    {
        EmvLog("CAPK TLV data is incomplete");
        return EMV_ERR_BAD_DATA;
    }

    ret = emv_capk_import(&capk);
    if (ret != EMV_OK)
    {
        EmvLog("emv_capk_import failed(%d)", ret);
        return ret;
    }

    // EmvLog("Import Successful(Capacity: %d, Count: %d)", g_emv_app_capks_container.capacity, g_emv_app_capks_container.count);
    return EMV_OK;
}

/**
 * @brief 根据 RID 和 CA公钥索引查询 CAPK。
 *
 * @param rid RID。
 * @param key_id CA公钥索引。
 *
 * @return 命中时返回 CAPK 指针，未命中返回 NULL。
 */
EMVCapk *emv_capk_find(const unsigned char *rid, unsigned char key_id)
{
    EMVCapk *capk = NULL;

    if (!rid)
    {
        EmvLog("Parameter `rid` is NULL");
        return NULL;
    }

    for (size_t i = 0; i < g_emv_app_capks_container.count; ++i)
    {
        capk = (EMVCapk *)g_emv_app_capks_container.items[i];

        if (capk->key_id == key_id && memcmp(capk->rid, rid, 5) == 0)
            return capk;
    }

    return NULL;
}
