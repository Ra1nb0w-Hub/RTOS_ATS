#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "mbedtls/sha1.h"

#include <string.h>

/**
 * @brief 复合动态数据认证
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_cda(void)
{
    int ret = EMV_OK;
    EMVIssuerPublicKeyInfo issuer_info = {0};
    EMVCapk *capk_info = NULL;

    // 获取并匹配 CAPK
    {
        unsigned char *rid = g_emv_session.app_parameter->aid;
        unsigned char key_index = 0;
        size_t key_index_len = sizeof(key_index);

        ret = emv_tlv_get(EMV_TAG_CA_PKI, &key_index, &key_index_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_CA_PKI, ret);
            emv_offline_auth_set_tvr(true, false);
            return ret;
        }

        capk_info = emv_capk_find(rid, key_index);
        if (capk_info == NULL)
        {
            EmvLog("CAPK not found(RID=%02X%02X%02X%02X%02X, Index=%02X)", rid[0], rid[1], rid[2], rid[3], rid[4], key_index);
            emv_offline_auth_set_tvr(false, false);
            return EMV_ERR_NOT_FOUND;
        }
    }

    // 发卡行公钥证书
    issuer_info.cert_len = sizeof(issuer_info.cert);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_CERTIFICATE, issuer_info.cert, &issuer_info.cert_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ISSUER_PUBLIC_KEY_CERTIFICATE, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 发卡行公钥指数
    issuer_info.exponent_len = sizeof(issuer_info.exponent);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_EXPONENT, issuer_info.exponent, &issuer_info.exponent_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ISSUER_PUBLIC_KEY_EXPONENT, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 发卡行公钥余数
    issuer_info.remainder_len = sizeof(issuer_info.remainder);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_REMAINDER, issuer_info.remainder, &issuer_info.remainder_len);
    if (ret != EMV_OK)
    {
        issuer_info.remainder_len = 0;
    }

    // 发卡行公钥恢复
    ret = emv_offline_auth_recover_issuer(capk_info, &issuer_info);
    if (ret != EMV_OK)
    {
        EmvLog("emv_offline_auth_recover_issuer failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // IC 卡公钥证书
    g_emv_session.icc_pk.cert_len = sizeof(g_emv_session.icc_pk.cert);
    ret = emv_tlv_get(EMV_TAG_ICC_PUBLIC_KEY_CERTIFICATE, g_emv_session.icc_pk.cert, &g_emv_session.icc_pk.cert_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ICC_PUBLIC_KEY_CERTIFICATE, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // IC 卡公钥指数
    g_emv_session.icc_pk.exponent_len = sizeof(g_emv_session.icc_pk.exponent);
    ret = emv_tlv_get(EMV_TAG_ICC_PUBLIC_KEY_EXPONENT, g_emv_session.icc_pk.exponent, &g_emv_session.icc_pk.exponent_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ICC_PUBLIC_KEY_EXPONENT, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // IC 卡公钥余数
    g_emv_session.icc_pk.remainder_len = sizeof(g_emv_session.icc_pk.remainder);
    ret = emv_tlv_get(EMV_TAG_ICC_PUBLIC_KEY_REMAINDER, g_emv_session.icc_pk.remainder, &g_emv_session.icc_pk.remainder_len);
    if (ret != EMV_OK)
    {
        g_emv_session.icc_pk.remainder_len = 0;
    }

    // IC 卡公钥恢复
    ret = emv_offline_auth_recover_icc(&issuer_info, &g_emv_session.icc_pk);
    if (ret != EMV_OK)
    {
        EmvLog("emv_offline_auth_recover_icc failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    g_emv_session.tvr.data_verify_result.not_executed = 1;
    g_emv_session.tvr.data_verify_result.cda_failed = 0;
    return EMV_OK;
}
