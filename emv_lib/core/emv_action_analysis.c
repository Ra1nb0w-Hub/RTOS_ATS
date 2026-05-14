#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>

/**
 * @brief 对比行为代码是否匹配
 * 
 * @param iac 卡片行为代码
 * @param tac 终端行为代码
 * @param tvr 终端验证结果
 * 
 * @return true:匹配成功 false:匹配失败
 */
static bool _match_action_code(const unsigned char iac[5], const unsigned char tac[5], const unsigned char tvr[5])
{
    for (unsigned char i = 0; i < 5; ++i)
    {
        if ((iac[i] & tvr[i]) != 0U || (tac[i] & tvr[i]) != 0U)
            return true;
    }

    return false;
}

/**
 * @brief 终端行为分析
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_action_analysis(bool *need_online)
{
    int ret = EMV_OK;
    unsigned char tvr[5] = {0};
    unsigned char iac_denial[5] = {0};
    unsigned char iac_online[5] = {0};
    unsigned char iac_default[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    unsigned char ac_type = EMV_GAC_TYPE_AAC;

    if (need_online == NULL)
    {
        EmvLog("Parameter `need_online` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    // 获取终端TAC相关数据
    if (g_emv_session.app_parameter == NULL)
    {
        EmvLog("Session `app_parameter` is invalid");
        return EMV_ERR_INVALID_PARAM;
    }

    // 获取TVR
    emv_tlv_get(EMV_TAG_TVR, tvr, NULL);

    // 获取卡片IAC相关数据
    emv_tlv_get(EMV_TAG_IAC_DENIAL, iac_denial, NULL);
    emv_tlv_get(EMV_TAG_IAC_ONLINE, iac_online, NULL);
    emv_tlv_get(EMV_TAG_IAC_DEFAULT, iac_default, NULL);

    // Step 1: 对比拒绝行为代码 (IAC-Denial / TAC-Denial)
    if (_match_action_code(iac_denial, g_emv_session.app_parameter->tac_denial, tvr))
    {
        EmvLog("GAC type is AAC (denial match)");
        emv_tlv_set(EMV_TAG_AUTH_RESPONSE_CODE, (unsigned char *)"Z1", 2);
        ac_type = EMV_GAC_TYPE_AAC;
    }
    // Step 2: 商户强制联机 或 终端支持联机时对比联机行为代码
    else if (g_emv_terminal.support_online && (g_emv_session.request.bForceOnline || _match_action_code(iac_online, g_emv_session.app_parameter->tac_online, tvr) || !g_emv_terminal.support_offline))
    {
        EmvLog("GAC type is ARQC%s", g_emv_session.request.bForceOnline ? " (force online)" : "");
        *need_online = true;
        ac_type = EMV_GAC_TYPE_ARQC;
    }
    // Step 3: 对比缺省行为代码 (IAC-Default / TAC-Default)
    else if (_match_action_code(iac_default, g_emv_session.app_parameter->tac_default, tvr))
    {
        EmvLog("GAC type is AAC (default match)");
        emv_tlv_set(EMV_TAG_AUTH_RESPONSE_CODE, (unsigned char *)"Z1", 2);
        ac_type = EMV_GAC_TYPE_AAC;
    }
    // Step 4: 无匹配，批准脱机交易
    else
    {
        EmvLog("GAC type is TC");
        emv_tlv_set(EMV_TAG_AUTH_RESPONSE_CODE, (unsigned char *)"Y1", 2);
        ac_type = EMV_GAC_TYPE_TC;
    }

    {
        unsigned char cdol_data[EMV_MAX_TAG_VALUE_LEN] = {0};
        size_t cdol_data_len = sizeof(cdol_data);

        // 构建CDOL1数据
        ret = emv_tools_build_dol_data(EMV_TAG_CDOL1, cdol_data, &cdol_data_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tools_build_dol_data failed(%d)", ret);
            return ret;
        }

        // 生成应用密文
        ret = emv_cmd_generate_ac(ac_type, cdol_data, cdol_data_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_cmd_generate_ac failed(%d)", ret);
            return ret;
        }

        g_emv_session.tsi.card_risk_management = 1;
    }

    {
        unsigned char cid = 0;
        EMVCryptogramInformationData cid_field = {0};

        ret = emv_tlv_get(EMV_TAG_CID, &cid, NULL);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_CID, ret);
            return ret;
        }

        // 解析CID
        ret = emv_tools_parse_cid(cid, &cid_field);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tools_parse_cid failed(%d)", ret);
            return ret;
        }

        if (cid_field.type == EMV_GAC_TYPE_AAC)
        {
            EmvLog("Card denied this transaction");
            return EMV_ERR_CARD_DENIED;
        }
    }

    return EMV_OK;
}
