#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

// 状态字字节1
static unsigned char gucStatusWord1 = 0x00;

// 状态字字节2
static unsigned char gucStatusWord2 = 0x00;

// 状态字
unsigned short g_status_word = 0x0000;

/**
 * @brief 按当前会话接口发送 APDU。
 * 
 * @param command 输入 APDU。
 * @param command_len APDU 长度。
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_transceive_apdu(unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len)
{
    int iRet = 0;
    size_t response_capacity = 0;

    if (!command || command_len == 0 || !response || !response_len)
    {
        EmvLog("Parameter invalid");
        return EMV_ERR_INVALID_PARAM;
    }

    response_capacity = *response_len;
    if (response_capacity < 2)
    {
        EmvLog("Parameter `response_len` is too short(%d)", response_capacity);
        return EMV_ERR_INVALID_PARAM;
    }

    gucStatusWord1 = gucStatusWord2 = 0x00;
    g_status_word = 0x0000;
    memset(response, 0x00, response_capacity);

    while (1)
    {
        *response_len = response_capacity;
        EmvHexLog("Send APDU", command, command_len);

        if (g_emv_session.interface_type == EMV_INTERFACE_CONTACTLESS)
        {
            if (!g_emv_reader.picc_transceive_apdu)
            {
                EmvLog("`picc_transceive_apdu` interface is NULL");
                return EMV_ERR_NOT_SUPPORTED;
            }

            iRet = g_emv_reader.picc_transceive_apdu(g_emv_reader.user_data, command, command_len, response, response_len);
            if (iRet < 0)
            {
                EmvLog("picc_transceive_apdu failed(%d)", iRet);
                return EMV_ERR_READER_IO;
            }
        }
        else
        {
            if (!g_emv_reader.icc_transceive_apdu)
            {
                EmvLog("`icc_transceive_apdu` interface is NULL");
                return EMV_ERR_NOT_SUPPORTED;
            }

            iRet = g_emv_reader.icc_transceive_apdu(g_emv_reader.user_data, command, command_len, response, response_len);
            if (iRet < 0)
            {
                EmvLog("icc_transceive_apdu failed(%d)", iRet);
                return EMV_ERR_READER_IO;
            }
        }

        if (*response_len < 2)
        {
            EmvLog("`response_len` is too short(%d)", *response_len);
            return EMV_ERR_BAD_DATA;
        }

        EmvHexLog("Recv APDU", response, *response_len);
        gucStatusWord1 = response[*response_len - 2];
        gucStatusWord2 = response[*response_len - 1];
        g_status_word = (unsigned short)(gucStatusWord1 << 8 | gucStatusWord2);
        *response_len -= 2;

        // EmvLog("Status Word: 0x%X", g_status_word);
        if (gucStatusWord1 == 0x6C)
        {
            // 继续发送之前的指令并更新Le字段
            command[command_len - 1] = gucStatusWord2;
            // EmvLog("Retry APDU, Continue send previous command, update `Le` to 0x%02X", gucStatusWord2);
            continue;
        }
        else if (gucStatusWord1 == 0x61)
        {
            // 使用GET RESPONSE命令读取数据
            memcpy(command, "\x00\xC0\x00\x00\x00", 5);
            command[4] = gucStatusWord2;
            command_len = 5U;
            // EmvLog("Retry APDU, Need use `GET RESPONSE` command, update `Le` to 0x%02X", gucStatusWord2);
            continue;
        }
        else
        {
            break;
        }
    }

    return EMV_OK;
}

/**
 * @brief 选择应用。
 *
 * @param name 应用名称
 * @param name_len 应用名称长度
 * @param next_occurrence true表示选择下一个匹配(P2=0x02)，false表示选择首个匹配(P2=0x00)
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_select(unsigned char *name, size_t name_len, bool next_occurrence, unsigned char *response, size_t *response_len)
{
    int ret = EMV_OK;
    unsigned char command[6 + EMV_MAX_AID_LEN] = {0};

    if (name == NULL)
    {
        EmvLog("Parameter `name` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (name_len == 0 || name_len > EMV_MAX_AID_LEN)
    {
        EmvLog("Parameter `name_len` is invalid(%d)", name_len);
        return EMV_ERR_INVALID_PARAM;
    }

    if (response == NULL)
    {
        EmvLog("Parameter `response` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (response_len == NULL || *response_len == 0)
    {
        EmvLog("Parameter `response_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0xA4;
    command[2] = 0x04;
    command[3] = next_occurrence ? 0x02 : 0x00;
    command[4] = (unsigned char)name_len;
    memcpy(&command[5], name, name_len);
    command[5 + name_len] = 0x00;
    
    ret = emv_transceive_apdu(command, 6 + name_len, response, response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        if (g_status_word == 0x6A82)
            return EMV_ERR_NOT_FOUND;

        EmvLog("[SELECT] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    // 查找 0x6F(FCI模板) 标签
    if (emv_tlv_find_tag(response, *response_len, EMV_TAG_FCI_TEMPLATE, 1, NULL, NULL) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_FCI_TEMPLATE);
        return EMV_ERR_NOT_FOUND;
    }
    
    // 查找 0x84(专用DF文件名称)
    if (emv_tlv_find_tag(response, *response_len, EMV_TAG_DF_NAME, 2, NULL, NULL) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_DF_NAME);
        return EMV_ERR_NOT_FOUND;
    }

    // 查找 0xA5(FCI数据)
    if (emv_tlv_find_tag(response, *response_len, EMV_TAG_FCI_DATA, 2, NULL, NULL) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_FCI_DATA);
        return EMV_ERR_NOT_FOUND;
    }

    return EMV_OK;
}

/**
 * @brief 获取应用处理选项。
 *
 * @param pdol PDOL值指针
 * @param pdol_len PDOL值长度
 * @param afl 应用文件定位器指针指针
 * @param afl_count 应用文件定位器数量指针
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_get_processing_options(unsigned char *pdol, size_t pdol_len, EMVAppFileLocator **afl, size_t *afl_count)
{
    int ret = EMV_OK;
    unsigned char command[6 + EMV_MAX_TAG_VALUE_LEN] = {0};
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);
    const unsigned char *data = NULL;
    size_t data_len = 0;

    if (pdol == NULL)
    {
        EmvLog("Parameter `pdol` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }
    
    if (pdol_len == 0 || pdol_len > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `pdol_len` is invalid(%d)", pdol_len);
        return EMV_ERR_INVALID_PARAM;
    }

    if (afl == NULL)
    {
        EmvLog("Parameter `afl` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (afl_count == NULL)
    {
        EmvLog("Parameter `afl_count` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x80;
    command[1] = 0xA8;
    command[2] = 0x00;
    command[3] = 0x00;
    command[4] = (unsigned char)pdol_len;
    memcpy(command + 5, pdol, pdol_len);
    command[5 + pdol_len] = 0x00;
    
    ret = emv_transceive_apdu(command, 6 + pdol_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[GET PROCESSING OPTIONS] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    // 查找 0x80 标签
    if (emv_tlv_find_tag(response, response_len, EMV_TAG_RESPONSE_TEMPLATE1, 1, &data, &data_len) == true)
    {
        size_t offset = 0;

        // 解析、保存应用交互特征
        {
            emv_tlv_set(EMV_TAG_AIP, data + offset, sizeof(EMVAppInterchangeProfile));
            emv_tools_parse_aip(data + offset, &g_emv_session.aip);

            offset += sizeof(EMVAppInterchangeProfile);
        }

        data = data + offset;
        data_len = data_len - offset;        
    }
    // 查找 0x77 标签
    else if (response[0] == EMV_TAG_RESPONSE_TEMPLATE2)
    {
        // 查找 0x82(AIP) 标签
        if (emv_tlv_find_tag(response, response_len, EMV_TAG_AIP, 2, &data, &data_len) == 0)
        {
            EmvLog("Not found tag `82`");
            return EMV_ERR_NOT_FOUND;
        }

        // 解析、保存应用交互特征
        if (data_len == sizeof(EMVAppInterchangeProfile))
        {
            emv_tlv_set(EMV_TAG_AIP, data, data_len);
            emv_tools_parse_aip(data, &g_emv_session.aip);
        }

        // 查找 0x94(AFL) 标签
        if (emv_tlv_find_tag(response, response_len, EMV_TAG_AFL, 2, &data, &data_len) == 0)
        {
            EmvLog("Not found tag `94`");
            return EMV_ERR_NOT_FOUND;
        }
    }
    else
    {
        EmvLog("unsupported response template(0x%02X)", response[0]);
        return EMV_ERR_BAD_DATA;
    }

    if (data_len % sizeof(EMVAppFileLocator) != 0)
    {
        EmvLog("Invalid AFL length(%d)", data_len);
        return EMV_ERR_BAD_DATA;
    }

    // 分配内存
    *afl_count = data_len / sizeof(EMVAppFileLocator);
    *afl = (EMVAppFileLocator *) malloc((*afl_count) * sizeof(EMVAppFileLocator));
    if (*afl == NULL)
    {
        EmvLog("malloc failed");
        return EMV_ERR_NO_MEMORY;
    }
    memset(*afl, 0, (*afl_count) * sizeof(EMVAppFileLocator));

    // 保存应用文件定位器
    for (size_t i = 0; i < *afl_count; i++)
    {
        memcpy(&(*afl)[i], data + i * sizeof(EMVAppFileLocator), sizeof(EMVAppFileLocator));
    }

    return EMV_OK;
}

/**
 * @brief 读取文件记录指令[READ RECORD]
 *
 * @param record_no 记录号
 * @param sfi 短文件标识符值
 * @param response 响应缓冲区指针
 * @param response_len 响应缓冲区长度指针
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_read_record(unsigned char record_no, unsigned char sfi, unsigned char *response, size_t *response_len)
{
    int ret = EMV_OK;
    unsigned char command[5] = {0};

    if (response == NULL)
    {
        EmvLog("Parameter `response` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (response_len == NULL || *response_len == 0)
    {
        EmvLog("Parameter `response_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0xB2;
    command[2] = record_no;
    command[3] = (unsigned char)(sfi << 3 | 0x04);
    command[4] = 0x00;
    ret = emv_transceive_apdu(command, sizeof(command), response, response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[READ RECORD] Status Word: 0x%X", g_status_word);

        // 6A83 未找到记录
        if (g_status_word == 0x6A83)
            return EMV_ERR_NOT_FOUND;

        return EMV_ERR_BAD_RESPONSE;
    }

    return EMV_OK;
}

/**
 * @brief 内部认证命令[INTERNAL AUTHENTICATE]
 * 
 * @param dynamic_data 动态数据
 * @param dynamic_data_len 动态数据长度
 * @param sdad 获取签名的动态应用数据(SDAD)的缓冲区。
 * @param sdad_len 获取签名的动态应用数据(SDAD)的数据长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_internal_authenticate(const unsigned char *dynamic_data, size_t dynamic_data_len, unsigned char *sdad, size_t *sdad_len)
{
    int ret = EMV_OK;
    unsigned char command[6 + EMV_MAX_TAG_VALUE_LEN] = {0};
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);

    if (dynamic_data == NULL)
    {
        EmvLog("Parameter `dynamic_data` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (dynamic_data_len == 0 || dynamic_data_len > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `dynamic_data_len` is invalid(%u)", (unsigned int)dynamic_data_len);
        return EMV_ERR_INVALID_PARAM;
    }

    if (sdad == NULL)
    {
        EmvLog("Parameter `response` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (sdad_len == NULL || *sdad_len == 0)
    {
        EmvLog("Parameter `sdad_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0x88;
    command[2] = 0x00;
    command[3] = 0x00;
    command[4] = (unsigned char)dynamic_data_len;
    memcpy(command + 5, dynamic_data, dynamic_data_len);
    command[5 + dynamic_data_len] = 0x00;

    ret = emv_transceive_apdu(command, 6 + dynamic_data_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[INTERNAL AUTHENTICATE] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    // 解析响应数据中的SDAD
    if (response[0] == EMV_TAG_RESPONSE_TEMPLATE1)
    {
        const unsigned char *value_ptr = NULL;
        size_t value_len = 0;

        if (emv_tlv_find_tag(response, response_len, EMV_TAG_RESPONSE_TEMPLATE1, 1, &value_ptr, &value_len) == false)
        {
            EmvLog("Response is missing tag `%X`", EMV_TAG_RESPONSE_TEMPLATE1);
            return EMV_ERR_BAD_DATA;
        }

        if (value_len > *sdad_len)
        {
            EmvLog("Parameter `sdad` buffer too small(%u < %u)", (unsigned int)*sdad_len, (unsigned int)value_len);
            return EMV_ERR_BUFFER_TOO_SMALL;
        }

        memcpy(sdad, value_ptr, value_len);
        *sdad_len = value_len;
        emv_tlv_set(EMV_TAG_SDAD, value_ptr, value_len);
    }
    else if (response[0] == EMV_TAG_RESPONSE_TEMPLATE2)
    {
        const unsigned char *value_ptr = NULL;
        size_t value_len = 0;

        if (emv_tlv_find_tag(response, response_len, EMV_TAG_SDAD, 2, &value_ptr, &value_len) == false)
        {
            EmvLog("Response is missing tag `%X`", EMV_TAG_SDAD);
            return EMV_ERR_BAD_DATA;
        }

        if (value_len > *sdad_len)
        {
            EmvLog("Parameter `sdad` buffer too small(%u < %u)", (unsigned int)*sdad_len, (unsigned int)value_len);
            return EMV_ERR_BUFFER_TOO_SMALL;
        }

        memcpy(sdad, value_ptr, value_len);
        *sdad_len = value_len;
        emv_tlv_set(EMV_TAG_SDAD, value_ptr, value_len);
    }
    else
    {
        EmvLog("[INTERNAL AUTHENTICATE] unsupported response template(0x%02X)", response[0]);
        return EMV_ERR_BAD_DATA;
    }

    return EMV_OK;
}

/**
 * @brief 生成应用密文指令[GENERATE APPLICATION CRYPTOGRAM]
 *
 * @param type 类型 0-AAC 1-TC 2-ARQC
 * @param cdol CDOL
 * @param cdol_len CDOL长度
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_generate_ac(unsigned char type, bool cda_request, const unsigned char *cdol, size_t cdol_len)
{
    int ret = EMV_OK;
    unsigned char command[6 + EMV_MAX_TAG_VALUE_LEN] = {0};
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);

    if (type > EMV_GAC_TYPE_ARQC)
    {
        EmvLog("Parameter `type` is invalid(%u)", type);
        return EMV_ERR_INVALID_PARAM;
    }

    if (cdol == NULL)
    {
        EmvLog("Parameter `cdol` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (cdol_len == 0 || cdol_len > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `cdol_len` is invalid(%u)", (unsigned int)cdol_len);
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x80;
    command[1] = 0xAE;
    command[2] = (unsigned char)(((type & 0x03U) << 6) | (cda_request ? 0x10U : 0x00U));
    command[3] = 0x00;
    command[4] = (unsigned char)cdol_len;
    if (cdol_len > 0)
        memcpy(command + 5, cdol, cdol_len);
    command[5 + cdol_len] = 0x00;

    ret = emv_transceive_apdu(command, 6 + cdol_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[GENERATE APPLICATION CRYPTOGRAM] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    if (response_len < 2)
    {
        EmvLog("[GENERATE APPLICATION CRYPTOGRAM] response too short(%u)", (unsigned int)response_len);
        return EMV_ERR_BAD_DATA;
    }

    if (response[0] == EMV_TAG_RESPONSE_TEMPLATE1)
    {
        if (response[1] < 11U)
        {
            EmvLog("template `0x80` value too short(%u)", (unsigned int)response[1]);
            return EMV_ERR_BAD_DATA;
        }

        emv_tlv_set(EMV_TAG_CID, response + 2, 1);
        emv_tlv_set(EMV_TAG_ATC, response + 3, 2);
        emv_tlv_set(EMV_TAG_AC, response + 5, 8);
        emv_tlv_set(EMV_TAG_IAD, response + 13, response_len - 13);
    }
    else if (response[0] == EMV_TAG_RESPONSE_TEMPLATE2)
    {
        size_t offset = 1, value_len = 0;

        ret = emv_tlv_parse_length(response, response_len, &offset, &value_len);
        if (ret != EMV_OK)
        {
            EmvLog("parse template `0x77` length failed(%d)", ret);
            return ret;
        }

        ret = emv_tlv_parse_data_with_save(response + offset, value_len);
        if (ret != EMV_OK)
        {
            EmvLog("parse template `0x77` data failed(%d)", ret);
            return ret;
        }
    }
    else
    {
        EmvLog("[GENERATE APPLICATION CRYPTOGRAM] unsupported response template(0x%02X)", response[0]);
        return EMV_ERR_BAD_DATA;
    }

    return EMV_OK;
}

/**
 * @brief 获取挑战码。
 * 
 * @param challenge 输出缓冲区。
 * @param challenge_len 获取到的挑战码长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_get_challenge(unsigned char *challenge, size_t *challenge_len)
{
    int ret = EMV_OK;
    unsigned char command[5] = {0};
    unsigned char response[32] = {0};
    size_t response_len = sizeof(response);

    if (challenge == NULL)
    {
        EmvLog("Parameter `challenge` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (challenge_len == NULL || *challenge_len == 0)
    {
        EmvLog("Parameter `challenge_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0x84;
    command[2] = 0x00;
    command[3] = 0x00;
    command[4] = 0x00;

    ret = emv_transceive_apdu(command, 5, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[GET CHALLENGE] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    if (response_len > *challenge_len)
    {
        EmvLog("Response length is greater than challenge_len(%d > %d)", response_len, *challenge_len);
        return EMV_ERR_INVALID_PARAM;
    }

    *challenge_len = response_len;
    memcpy(challenge, response, *challenge_len);
    return EMV_OK;
}

/**
 * @brief 获取数据指令[GET DATA]
 * 
 * @param tag 标签
 * @param value 数据指针
 * @param value_len 数据指针长度指针
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_get_data(uint16_t tag, unsigned char *value, size_t *value_len)
{
    int ret = EMV_OK;
    unsigned char command[] = {0x80, 0xCA, 0x00, 0x00, 0x00};
    unsigned char response[256] = {0};
    size_t response_len = sizeof(response);
    const unsigned char *value_ptr = NULL;
    size_t actual_value_len = 0;

    if (value == NULL)
    {
        EmvLog("Parameter `value` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    command[2] = (unsigned char)(tag >> 8);
    command[3] = (unsigned char)(tag & 0xFFU);

    ret = emv_transceive_apdu(command, sizeof(command), response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[GET DATA] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    if (!emv_tlv_find_tag(response, response_len, tag, 1, &value_ptr, &actual_value_len))
    {
        EmvLog("[GET DATA] response does not contain tag 0x%X", tag);
        return EMV_ERR_BAD_DATA;
    }

    if (value_len)
    {
        if (actual_value_len > *value_len)
        {
            EmvLog("[GET DATA] response tag `0x%X` value length(%d) is greater than output buffer length(%d)", tag, actual_value_len, *value_len);
            return EMV_ERR_BAD_DATA;
        }

        *value_len = actual_value_len;
    }

    memcpy(value, value_ptr, actual_value_len);
    emv_tlv_set(tag, value_ptr, actual_value_len);
    return EMV_OK;
}

/**
 * @brief 外部认证指令[EXTERNAL AUTHENTICATE]
 *
 * @param arpc 发卡行认证数据。
 * @param arpc_len 发卡行认证数据长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_external_authenticate(const unsigned char *arpc, size_t arpc_len)
{
    int ret = EMV_OK;
    unsigned char command[5 + EMV_MAX_TAG_VALUE_LEN] = {0};
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);

    if (arpc == NULL || arpc_len == 0)
    {
        EmvLog("Parameter `arpc` is NULL or `arpc_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (arpc_len > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `arpc_len` is too long(%u)", (unsigned int)arpc_len);
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0x82;
    command[2] = 0x00;
    command[3] = 0x00;
    command[4] = (unsigned char)arpc_len;
    memcpy(command + 5, arpc, arpc_len);

    ret = emv_transceive_apdu(command, 5 + arpc_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[EXTERNAL AUTHENTICATE] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    return EMV_OK;
}

/**
 * @brief 发卡行脚本指令
 *
 * @param script_cmd 脚本命令数据(Tag 86的Value)。
 * @param script_cmd_len 脚本命令数据长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_issuer_script(const unsigned char *script_cmd, size_t script_cmd_len)
{
    int ret = EMV_OK;
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);

    if (script_cmd == NULL || script_cmd_len == 0)
    {
        EmvLog("Parameter `script_cmd` is NULL or `script_cmd_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_transceive_apdu((unsigned char *)script_cmd, script_cmd_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if ((g_status_word & 0xFF00U) != 0x9000U && (g_status_word & 0xFF00U) != 0x6100U)
    {
        EmvLog("[ISSUER SCRIPT] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    return EMV_OK;
}

/**
 * @brief 持卡人验证指令[VERIFY]
 * 
 * 明文：PIN数据需要封装为PIN Block格式。
 * 密文：PIN数据需要封装为PIN Block格式，并且需要使用ICC公钥进行加密。
 * 
 * @param data 输入缓冲区。
 * @param data_len 输入数据长度。
 * @param plaintext 是否明文验证。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cmd_cardholder_verify(unsigned char *data, size_t data_len, bool plaintext)
{
    int ret = EMV_OK;
    unsigned char command[133] = {0};
    unsigned char response[255] = {0};
    size_t response_len = sizeof(response);

    if (data == NULL)
    {
        EmvLog("Parameter `data` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (data_len < 8 || data_len > 128)
    {
        EmvLog("Parameter `data_len` is out of range (must be >= 8 and <= 128): %d", data_len);
        return EMV_ERR_INVALID_PARAM;
    }

    command[0] = 0x00;
    command[1] = 0x20;
    command[2] = 0x00;
    command[3] = plaintext ? 0x80 : 0x88;
    command[4] = (unsigned char)data_len;
    memcpy(&command[5], data, data_len);

    ret = emv_transceive_apdu(command, 5 + data_len, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_transceive_apdu failed(%d)", ret);
        return ret;
    }

    if (g_status_word != 0x9000U)
    {
        EmvLog("[PINBLOCK VERIFY] Status Word: 0x%X", g_status_word);
        return EMV_ERR_BAD_RESPONSE;
    }

    return EMV_OK;
}
