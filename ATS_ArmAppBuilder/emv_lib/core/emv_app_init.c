#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>
#include <stdlib.h>

/**
 * @brief 构建GPO请求数据
 * 
 * @param pdol PDOL数据缓冲区指针
 * @param pdol_len PDOL数据缓冲区长度指针
 * 
 * @return 0:成功 <0:失败
 */
static int _build_gpo_request_data(unsigned char *pdol, size_t *pdol_len)
{
    int ret = EMV_OK;
    unsigned char data[512] = {0};
    size_t data_len = sizeof(data);

    if (pdol == NULL)
    {
        EmvLog("Parameter `pdol` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (pdol_len == NULL || *pdol_len == 0)
    {
        EmvLog("Parameter `pdol_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_tools_build_dol_data(EMV_TAG_PDOL, data, &data_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_build_dol_data failed(%d)", ret);
        return ret;
    }

    return emv_tlv_build(0x83U, data, data_len, pdol, pdol_len);
}

/**
 * @brief 获取卡片处理选项
 * 
 * @param afl 应用文件定位器数组指针
 * @param afl_count 应用文件定位器数组元素数量指针
 * 
 * @return 0:成功 <0:失败
 */
static int _get_processing_options(EMVAppFileLocator **afl_array, size_t *afl_count)
{
    int ret = EMV_OK;
    unsigned char pdol[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t pdol_len = sizeof(pdol);

    // 构建GPO请求数据
    ret = _build_gpo_request_data(pdol, &pdol_len);
    if (ret != EMV_OK)
    {
        EmvLog("_build_gpo_request_data failed(%d)", ret);
        return ret;
    }

    // 发送GPO请求
    ret = emv_cmd_get_processing_options(pdol, pdol_len, afl_array, afl_count);
    if (ret != EMV_OK)
    {
        EmvLog("emv_cmd_get_processing_options failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 根据AFL读取应用记录。
 * 
 * @param afl_array 应用文件定位器数组指针
 * @param afl_count 应用文件定位器数组元素数量
 * 
 * @return 0:成功 <0:失败
 */
static int _read_application_record(EMVAppFileLocator *afl_array, size_t afl_count)
{
    int ret = EMV_OK;
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);

    if (afl_array == NULL)
    {
        EmvLog("Parameter `afl_array` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (afl_count == 0)
    {
        EmvLog("Parameter `afl_count` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    EmvLog("AFL total count: %u", afl_count);
    for (size_t i = 0; i < afl_count; ++i)
    {
        const EMVAppFileLocator *afl = &afl_array[i];
        unsigned char sfi = 0;

        if (afl == NULL || afl->sfi == 0 || afl->first_record_no == 0 || afl->last_record_no < afl->first_record_no)
        {
            EmvLog("AFL[%u] is invalid", i);
            return EMV_ERR_BAD_DATA;
        }

        sfi = (afl->sfi & 0xF8) >> 3;
        EmvLog("AFL[%u] Info: SFI=0x%02X, AFR=%u, ALR=%u, ARC=%u", i, sfi, afl->first_record_no, afl->last_record_no, afl->offline_record_count);
        for (unsigned char record_no = afl->first_record_no; record_no <= afl->last_record_no; ++record_no)
        {
            const unsigned char *value_ptr = NULL;
            size_t value_len = 0;

            memset(response, 0, sizeof(response));
            response_len = sizeof(response);

            // 读取文件记录
            EmvLog("Current Read record: SFI=0x%02X, Record=%u", sfi, record_no);
            ret = emv_cmd_read_record(record_no, sfi, response, &response_len);
            if (ret != EMV_OK)
            {
                EmvLog("emv_cmd_read_record failed(%d)", ret);
                return ret;
            }

            // 查找 0x70(记录模板) 标签
            if (emv_tlv_find_tag(response, response_len, EMV_TAG_RECORD_TEMPLATE, 1, &value_ptr, &value_len) == 0)
            {
                EmvLog("Not found tag `0x%X`", EMV_TAG_RECORD_TEMPLATE);
                return EMV_ERR_BAD_DATA;
            }

            // 保存记录中的Tag数据
            ret = emv_tlv_parse_data_with_save(value_ptr, value_len);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tlv_parse_data_with_save failed(%d)", ret);
                return ret;
            }

            // 检查当前记录是否是脱机认证记录
            if (afl->offline_record_count > 0 && ((record_no - afl->first_record_no) < afl->offline_record_count))
            {
                // 检查SFI是否处于1-10之间，如果是不记录70标签长度，否则记录70标签长度
                if (sfi >= 1 && sfi <= 10)
                    ret = emv_offline_auth_add_record(value_ptr, value_len);
                else
                    ret = emv_offline_auth_add_record(response, response_len);

                if (ret != EMV_OK)
                {
                    EmvLog("emv_offline_auth_add_record failed(%d)", ret);
                    return ret;
                }
            }
        }
    }

    return EMV_OK;
}

/**
 * @brief 初始化应用
 * 
 * @return 0:成功 <0:失败
 */
int emv_app_init(void)
{
    int ret = EMV_OK;
    EMVAppFileLocator *afl_array = NULL;
    size_t afl_count = 0;

    // 获取卡片处理选项
    ret = _get_processing_options(&afl_array, &afl_count);
    if (ret != EMV_OK)
    {
        EmvLog("_get_processing_options failed(%d)", ret);
        goto exit;
    }

    // 根据AFL读取应用记录
    ret = _read_application_record(afl_array, afl_count);
    if (ret != EMV_OK)
    {
        EmvLog("_read_application_record failed(%d)", ret);
        goto exit;
    }

    ret = EMV_OK;
exit:
    if (afl_array)
        free(afl_array);

    return ret;
}
