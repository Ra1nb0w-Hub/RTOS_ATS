#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>
#include <stdlib.h>

// 应用参数容器
static EMVContainer g_emv_app_parameter_container = {0};

/**
 * @brief 获取应用参数容器。
 *
 * @return 应用参数容器指针。
 */
EMVContainer *emv_app_parameter_get_container(void)
{
    return &g_emv_app_parameter_container;
}

/**
 * @brief 清空应用参数容器。
 */
void emv_app_parameter_clear(void)
{
    emv_tools_container_clear(&g_emv_app_parameter_container);
}

/**
 * @brief 导入应用参数到容器。
 * 
 * @param new_app_parameter 应用参数
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_app_parameter_import(const EMVAppParameter *new_app_parameter)
{
    int ret = EMV_OK;
    EMVAppParameter *temp_app_parameter = NULL;

    if (!new_app_parameter)
    {
        EmvLog("Parameter `new_app_parameter` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (new_app_parameter->aid_len == 0 || new_app_parameter->aid_len > EMV_MAX_AID_LEN)
    {
        EmvLog("Parameter `new_app_parameter` aid_len is invalid(%d)", new_app_parameter->aid_len);
        return EMV_ERR_INVALID_PARAM;
    }

    if (new_app_parameter->asi > 1)
    {
        EmvLog("Parameter `new_app_parameter` asi is invalid(%d)", new_app_parameter->asi);
        return EMV_ERR_INVALID_PARAM;
    }

    if (new_app_parameter->ddol_len > EMV_MAX_DDOL_LEN)
    {
        EmvLog("Parameter `new_app_parameter` ddol_len is invalid(%d)", new_app_parameter->ddol_len);
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查是否存在相同AID的应用参数条目, 如果存在则更新
    for (size_t i = 0; i < g_emv_app_parameter_container.count; ++i)
    {
        temp_app_parameter = (EMVAppParameter *)g_emv_app_parameter_container.items[i];

        if ((size_t)temp_app_parameter->aid_len == new_app_parameter->aid_len && memcmp(temp_app_parameter->aid, new_app_parameter->aid, new_app_parameter->aid_len) == 0)
        {
            memcpy(temp_app_parameter, new_app_parameter, sizeof(EMVAppParameter));
            return EMV_OK;
        }
    }

    // 初始化容器
    ret = emv_tools_container_init(&g_emv_app_parameter_container, EMV_DEFAULT_APP_PARAMETER_CAPACITY);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_init failed(%d)", ret);
        return ret;
    }

    temp_app_parameter = (EMVAppParameter *)malloc(sizeof(EMVAppParameter));
    if (temp_app_parameter == NULL)
    {
        EmvLog("EMVAppParameter malloc failed(%d bytes)", sizeof(EMVAppParameter));
        return EMV_ERR_NO_MEMORY;
    }
    memcpy(temp_app_parameter, new_app_parameter, sizeof(EMVAppParameter));

    ret = emv_tools_container_add(&g_emv_app_parameter_container, (void *)temp_app_parameter);
    if (ret != EMV_OK)
    {
        if (temp_app_parameter)
            free(temp_app_parameter);

        EmvLog("emv_tools_container_add failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 查找 AID 是否正确匹配。
 *
 * @param aid AID。
 * @param aid_len AID 长度。
 *
 * @return 匹配成功返回应用参数指针，失败返回NULL。
 */
EMVAppParameter *emv_app_parameter_match(const unsigned char *aid, size_t aid_len)
{
    EMVAppParameter *app_parameter = NULL;

    if (!aid)
    {
        EmvLog("Parameter `aid` is NULL");
        return NULL;
    }

    if (aid_len == 0)
    {
        EmvLog("Parameter `aid_len` is 0");
        return NULL;
    }

    for (size_t i = 0; i < g_emv_app_parameter_container.count; ++i)
    {
        app_parameter = (EMVAppParameter *)g_emv_app_parameter_container.items[i];

        // 优先检查是否完整匹配
        if ((size_t)app_parameter->aid_len == aid_len && memcmp(app_parameter->aid, aid, aid_len) == 0)
            return app_parameter;
        
        // 如果支持部分匹配，检查是否匹配
        if (app_parameter->asi == 0 && app_parameter->aid_len < aid_len && memcmp(app_parameter->aid, aid, app_parameter->aid_len) == 0)
            return app_parameter;
    }

    return NULL;
}

/**
 * @brief 从 TLV 数据导入一条应用参数到容器。
 *
 * @param tlv_data 应用参数 TLV 缓冲区。
 * @param tlv_length TLV 缓冲区长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_app_parameter_import_from_tlv(const unsigned char *tlv_data, unsigned int tlv_length)
{
    int ret = EMV_OK;
    size_t offset = 0;
    EMVAppParameter app_parameter = {0};
    int state = 0;

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

        value_ptr = tlv_data + offset;
        switch (tag)
        {
        case 0x9F06:// AID
            if (value_length == 0 || value_length > EMV_MAX_AID_LEN)
            {
                EmvLog("The AID length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.aid_len = (unsigned char)value_length;
            memcpy(app_parameter.aid, value_ptr, value_length);
            state |= 0x01;
            break;
        case 0xDF01:// ASI
            if (value_length != 1)
            {
                EmvLog("The ASI length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.asi = value_ptr[0] ? 1 : 0;
            state |= 0x02;
            break;
        case 0x9F08:// 应用版本号
            if (value_length != 2)
            {
                EmvLog("The application version length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.app_version, value_ptr, 2);
            state |= 0x04;
            break;
        case 0xDF11:// TAC缺省
            if (value_length != 5)
            {
                EmvLog("The TAC default length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.tac_default, value_ptr, 5);
            state |= 0x08;
            break;
        case 0xDF12:// TAC联机
            if (value_length != 5)
            {
                EmvLog("The TAC online length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.tac_online, value_ptr, 5);
            state |= 0x10;
            break;
        case 0xDF13:// TAC拒绝
            if (value_length != 5)
            {
                EmvLog("The TAC denial length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.tac_denial, value_ptr, 5);
            state |= 0x20;
            break;
        case 0x9F1B:// 终端最低限额
            if (value_length != 4)
            {
                EmvLog("The floor limit length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.floor_limit, value_ptr, 4);
            state |= 0x40;
            break;
        case 0xDF14:// DDOL缺省
            if (value_length > EMV_MAX_DDOL_LEN)
            {
                EmvLog("The DDOL length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.ddol_len = (unsigned char)value_length;
            if (value_length > 0)
                memcpy(app_parameter.ddol, value_ptr, value_length);
            state |= 0x80;
            break;
        case 0xDF15:// 偏置随机选择的阈值
            if (value_length != 4)
            {
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.threshold, value_ptr, 4);
            break;
        case 0xDF16:// 偏置随机选择的最大目标百分数
            if (value_length != 1)
            {
                EmvLog("The max target percentage length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.max_target_percentage = value_ptr[0];
            break;
        case 0xDF17:// 随机选择的目标百分数
            if (value_length != 1)
            {
                EmvLog("The target percentage length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.target_percentage = value_ptr[0];
            break;
        case 0xDF18:// 终端联机PIN支持能力
            if (value_length != 1)
            {
                EmvLog("The online PIN flag length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            app_parameter.online_pin_flag = value_ptr[0] ? 1 : 0;
            break;
        case 0xDF19:// 非接触读写器脱机最低限额
            if (value_length != 6)
            {
                EmvLog("The contactless floor limit length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.cl_floor_limit, value_ptr, 6);
            break;
        case 0xDF20:// 非接触读写器脱机交易限额
            if (value_length != 6)
            {
                EmvLog("The contactless transaction limit length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.cl_trans_limit, value_ptr, 6);
            break;
        case 0xDF21:// 非接触读写器执行CVM限额
            if (value_length != 6)
            {
                EmvLog("The contactless CVM limit length is invalid(%d)", value_length);
                return EMV_ERR_BAD_DATA;
            }
            memcpy(app_parameter.cl_cvm_limit, value_ptr, 6);
            break;
        default:
            break;
        }

        offset += value_length;
    }

    if (state != 0xFF)
    {
        EmvLog("App Parameter TLV data is incomplete");
        return EMV_ERR_BAD_DATA;
    }

    ret = emv_app_parameter_import(&app_parameter);
    if (ret != EMV_OK)
    {
        EmvLog("emv_app_parameter_import failed(%d)", ret);
        return ret;
    }

    // EmvLog("Import Successful(Capacity: %d, Count: %d)", g_emv_app_parameter_container.capacity, g_emv_app_parameter_container.count);
    return EMV_OK;
}
