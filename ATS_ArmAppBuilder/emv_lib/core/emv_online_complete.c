#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>

/**
 * @brief 处理发卡行脚本模板(Tag 71 或 Tag 72)
 *
 * @param script 脚本数据缓冲区(可能包含多个TLV模板)
 * @param script_len 脚本数据长度
 * @param template_tag 目标模板标签(0x71 或 0x72)
 *
 * @return EMV_OK 表示全部脚本命令执行成功，否则返回错误码。
 */
static int _process_issuer_script(const unsigned char *script, size_t script_len, unsigned short template_tag)
{
    int ret = EMV_OK;
    bool executed = false;
    const unsigned char *template_data = NULL;
    size_t template_len = 0;

    if (script == NULL || script_len == 0)
        return EMV_OK;

    if (template_tag != EMV_TAG_ISSUER_SCRIPT_TEMPLATE_1 && template_tag != EMV_TAG_ISSUER_SCRIPT_TEMPLATE_2)
    {
        EmvLog("Parameter `template_tag` is invalid(0x%X)", template_tag);
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查是否存在目标模板Tag
    if (emv_tlv_find_tag(script, script_len, template_tag, 1, &template_data, &template_len) == false)
    {
        // 目标模板不存在，直接返回成功
        return EMV_OK;
    }

    // 查找并执行发卡行脚本命令(Tag 86)
    {
        const unsigned char *cmd_data = NULL;
        size_t cmd_len = 0;

        if (emv_tlv_find_tag(template_data, template_len, EMV_TAG_ISSUER_SCRIPT_COMMAND, 1, &cmd_data, &cmd_len) && cmd_len > 0)
        {
            ret = emv_cmd_issuer_script(cmd_data, cmd_len);
            if (ret != EMV_OK)
            {
                EmvLog("Issuer script command failed(%d), template=0x%02X", ret, template_tag);
                return ret;
            }

            executed = true;
        }
    }

    if (executed)
        g_emv_session.tsi.issuing_bank_script = 1;

    return EMV_OK;
}

/**
 * @brief 联机交易完成处理
 *
 * @param arc 授权响应码(2字节)，可为NULL表示联机失败
 * @param tlv 服务器返回的TLV数据(可能包含Tag 91/71/72等)
 * @param tlv_len TLV数据长度
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_online_complete(const char *arc, const unsigned char *tlv, size_t tlv_len)
{
    int ret = EMV_OK;
    unsigned char ac_type = EMV_GAC_TYPE_AAC;
    const unsigned char *arpc = NULL;
    size_t arpc_len = 0;

    // 从TLV数据中解析Tag 91(ARPC)
    if (tlv != NULL && tlv_len > 0)
        emv_tlv_find_tag(tlv, tlv_len, EMV_TAG_ISSUER_AUTH_DATA, 1, &arpc, &arpc_len);

    // Step A: 设置授权响应码(ARC)
    if (arc != NULL)
        emv_tlv_set(EMV_TAG_AUTH_RESPONSE_CODE, (unsigned char *)arc, 2);

    // Step B: 发卡行认证(EXTERNAL AUTHENTICATE)
    if (arpc != NULL && arpc_len > 0)
    {
        emv_tlv_set(EMV_TAG_ISSUER_AUTH_DATA, arpc, arpc_len);

        ret = emv_cmd_external_authenticate(arpc, arpc_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_cmd_external_authenticate failed(%d)", ret);
            g_emv_session.tvr.action_analysis_result.icc_verify_failed = 1;
        }

        g_emv_session.tsi.issuing_bank_verification = 1;
    }

    // Step C: 处理发卡行脚本模板1(Tag 71) — 在二次GAC之前
    ret = _process_issuer_script(tlv, tlv_len, EMV_TAG_ISSUER_SCRIPT_TEMPLATE_1);
    if (ret != EMV_OK)
    {
        EmvLog("Process issuer script template 1 failed(%d)", ret);
        g_emv_session.tvr.action_analysis_result.gac_before_failed = 1;
    }

    // Step D: 二次GENERATE AC
    {
        unsigned char cdol_data[EMV_MAX_TAG_VALUE_LEN] = {0};
        size_t cdol_data_len = sizeof(cdol_data);
        bool cda_request = false;

        // 判断AC类型：发卡行批准(ARC="00")则请求TC，否则请求AAC
        if (arc != NULL && arc[0] == '0' && arc[1] == '0')
            ac_type = EMV_GAC_TYPE_TC;
        else
            ac_type = EMV_GAC_TYPE_AAC;

        // 判断是否请求CDA
        if (g_emv_session.aip.cda && g_emv_session.terminal_capabilities.security.cda && g_emv_session.icc_pk.modulus_len > 0)
            cda_request = true;

        // 构建CDOL2数据，如果CDOL2不存在则使用CDOL1
        ret = emv_tools_build_dol_data(EMV_TAG_CDOL2, cdol_data, &cdol_data_len);
        if (ret != EMV_OK)
        {
            EmvLog("Build CDOL2 failed(%d), try CDOL1", ret);
            cdol_data_len = sizeof(cdol_data);
            ret = emv_tools_build_dol_data(EMV_TAG_CDOL1, cdol_data, &cdol_data_len);
            if (ret != EMV_OK)
            {
                EmvLog("Build CDOL1 failed(%d)", ret);
                return ret;
            }
        }

        ret = emv_cmd_generate_ac(ac_type, cda_request, cdol_data, cdol_data_len);
        if (ret != EMV_OK)
        {
            EmvLog("Second GENERATE AC failed(%d)", ret);
            return ret;
        }

        // CDA验证：验证二次GAC响应中的SDAD
        if (cda_request)
        {
            ret = emv_offline_auth_cda_verify_sdad();
            if (ret != EMV_OK)
            {
                EmvLog("Second GAC CDA verify failed(%d)", ret);
                g_emv_session.tvr.data_verify_result.cda_failed = 1;
            }
        }
    }

    // 解析CID，检查卡片最终决定
    {
        unsigned char cid = 0;
        EMVCryptogramInformationData cid_field = {0};

        ret = emv_tlv_get(EMV_TAG_CID, &cid, NULL);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_CID, ret);
            return ret;
        }

        ret = emv_tools_parse_cid(cid, &cid_field);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tools_parse_cid failed(%d)", ret);
            return ret;
        }

        EmvLog("Second GAC CID type: %d (requested: %d)", cid_field.type, ac_type);

        if (cid_field.type == EMV_GAC_TYPE_AAC)
        {
            EmvLog("Card denied transaction (second GAC)");
        }
    }

    // Step E: 处理发卡行脚本模板2(Tag 72) — 在二次GAC之后
    ret = _process_issuer_script(tlv, tlv_len, EMV_TAG_ISSUER_SCRIPT_TEMPLATE_2);
    if (ret != EMV_OK)
    {
        EmvLog("Process issuer script template 2 failed(%d)", ret);
        g_emv_session.tvr.action_analysis_result.gac_after_failed = 1;
    }

    return EMV_OK;
}
