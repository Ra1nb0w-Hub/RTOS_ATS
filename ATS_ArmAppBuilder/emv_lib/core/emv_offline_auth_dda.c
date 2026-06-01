#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "mbedtls/sha1.h"
#include <string.h>

/**
 * @brief 加载 DDOL，优先使用卡片 9F49，缺失时回退应用参数缺省值。
 * 
 * @param ddol DDOL 列表。
 * @param ddol_len DDOL 列表长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _load_ddol(unsigned char *ddol, size_t *ddol_len)
{
    int ret = EMV_OK;

    if (!ddol || !ddol_len || *ddol_len == 0)
    {
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_tlv_get(EMV_TAG_DDOL, ddol, ddol_len);
    if (ret == EMV_OK && *ddol_len > 0)
    {
        return EMV_OK;
    }

    if (g_emv_session.app_parameter == NULL)
    {
        EmvLog("selected app parameter is NULL");
        return EMV_ERR_BAD_DATA;
    }

    if (g_emv_session.app_parameter->ddol_len == 0)
    {
        EmvLog("DDOL is empty");
        return EMV_ERR_BAD_DATA;
    }

    if (g_emv_session.app_parameter->ddol_len > *ddol_len)
    {
        EmvLog("DDOL length invalid(%u)", g_emv_session.app_parameter->ddol_len);
        return EMV_ERR_BAD_DATA;
    }

    *ddol_len = g_emv_session.app_parameter->ddol_len;
    memcpy(ddol, g_emv_session.app_parameter->ddol, *ddol_len);
    return EMV_OK;
}

/**
 * @brief 检查 DDOL 是否包含 9F37。
 * 
 * @param ddol DDOL 列表。
 * @param ddol_len DDOL 列表长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _validate_ddol(const unsigned char *ddol, size_t ddol_len)
{
    bool found = false;

    if (!ddol || ddol_len == 0)
    {
        return EMV_ERR_INVALID_PARAM;
    }

    for (size_t i = 0; i < ddol_len; ++i)
    {
        if (i + 3U > ddol_len)
        {
            break;
        }

        if (memcmp(ddol + i, "\x9F\x37\x04", 3) == 0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        EmvLog("DDOL is missing tag `9F37`");
        return EMV_ERR_BAD_DATA;
    }

    return EMV_OK;
}

/**
 * @brief 依据DDOL构建动态数据。
 * 
 * @param ddol DDOL 列表。
 * @param ddol_len DDOL 列表长度。
 * @param dynamic_data 获取动态数据的缓冲区。
 * @param dynamic_data_len 获取动态数据的数据长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _build_dynamic_data(const unsigned char *ddol, size_t ddol_len, unsigned char *dynamic_data, size_t *dynamic_data_len)
{
    int ret = EMV_OK;
    size_t offset = 0;
    size_t data_len = 0;

    if (ddol == NULL)
    {
        EmvLog("Parameter `ddol` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (ddol_len == 0)
    {
        EmvLog("Parameter `ddol_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    while (offset < ddol_len)
    {
        unsigned short tag = 0;
        size_t value_len = 0;
        unsigned char value[EMV_MAX_TAG_VALUE_LEN] = {0};

        ret = emv_tlv_parse_tag(ddol, ddol_len, &offset, &tag);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_tag failed(%d)", ret);
            return ret;
        }

        ret = emv_tlv_parse_length(ddol, ddol_len, &offset, &value_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_length failed(%d)", ret);
            return ret;
        }

        if (value_len > sizeof(value))
        {
            EmvLog("Tag `%X` length invalid(%u)", tag, (unsigned int)value_len);
            return EMV_ERR_BUFFER_TOO_SMALL;
        }

        ret = emv_tlv_get(tag, value, &value_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `%X` failed(%d)", tag, ret);
            return ret;
        }

        if (data_len + value_len > *dynamic_data_len)
        {
            EmvLog("Parameter `dynamic_data` buffer too small");
            return EMV_ERR_BUFFER_TOO_SMALL;
        }

        memcpy(dynamic_data + data_len, value, value_len);
        data_len += value_len;
    }

    *dynamic_data_len = data_len;
    return EMV_OK;
}

/**
 * @brief 计算并验证SDAD摘要。
 * 
 * @param sdad_recovered 恢复的SDAD摘要。
 * @param sdad_len SDAD摘要长度。
 * @param dynamic_data 动态数据。
 * @param dynamic_data_len 动态数据长度。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _verify_sdad_hash(const unsigned char *sdad_recovered, size_t sdad_len, const unsigned char *dynamic_data, size_t dynamic_data_len)
{
    int ret = EMV_OK;
    mbedtls_sha1_context sha1_ctx;
    unsigned char hash_data[EMV_SHA1_LEN] = {0};
    const size_t hash_offset = sdad_len - 21U;

    if (!sdad_recovered || sdad_len < 21U || !dynamic_data || dynamic_data_len == 0)
    {
        return EMV_ERR_INVALID_PARAM;
    }

    mbedtls_sha1_init(&sha1_ctx);
    ret = mbedtls_sha1_starts_ret(&sha1_ctx);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_starts_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_STARTS;
        goto exit;
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, sdad_recovered + 1U, hash_offset - 1U);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, dynamic_data, dynamic_data_len);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    ret = mbedtls_sha1_finish_ret(&sha1_ctx, hash_data);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_finish_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_FINISH;
        goto exit;
    }

    if (memcmp(hash_data, sdad_recovered + hash_offset, EMV_SHA1_LEN) != 0)
    {
        EmvLog("SDAD hash mismatch");
        ret = EMV_ERR_HASH_VERIFY;
        goto exit;
    }

    ret = EMV_OK;
exit:
    mbedtls_sha1_free(&sha1_ctx);
    return ret;
}

/**
 * @brief 动态数据认证
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_dda(void)
{
    int ret = EMV_OK;
    EMVIssuerPublicKeyInfo issuer_info = {0};
    EMVCapk *capk_info = NULL;
    unsigned char ddol[EMV_MAX_DDOL_LEN] = {0};
    size_t ddol_len = sizeof(ddol);
    unsigned char dynamic_data[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t dynamic_data_len = sizeof(dynamic_data);
    unsigned char sdad[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t sdad_len = sizeof(sdad);
    unsigned char recovered_data[EMV_MAX_MODULUS_LEN] = {0};

    // 加载并检查 DDOL
    ret = _load_ddol(ddol, &ddol_len);
    if (ret != EMV_OK)
    {
        EmvLog("_load_ddol failed(%d)", ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 验证DDOL数据
    ret = _validate_ddol(ddol, ddol_len);
    if (ret != EMV_OK)
    {
        EmvLog("_validate_ddol failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 构建动态数据
    ret = _build_dynamic_data(ddol, ddol_len, dynamic_data, &dynamic_data_len);
    if (ret != EMV_OK)
    {
        EmvLog("_build_dynamic_data failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

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

    // 内部指令并获取SDAD
    ret = emv_cmd_internal_authenticate(dynamic_data, dynamic_data_len, sdad, &sdad_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_cmd_internal_authenticate failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 解密SDAD数据域
    ret = emv_tools_rsa_public_key(g_emv_session.icc_pk.modulus, g_emv_session.icc_pk.modulus_len, g_emv_session.icc_pk.exponent, g_emv_session.icc_pk.exponent_len, sdad, sdad_len, recovered_data);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_rsa_public_key failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 检查SDAD数据恢复格式是否正确
    if (recovered_data[0] != EMV_FLAG_HEADER || recovered_data[1] != EMV_FLAG_FORMAT_SDAD || recovered_data[2] != EMV_HASH_ALG_SHA1 || recovered_data[sdad_len - 1U] != EMV_FLAG_TRAILER)
    {
        EmvLog("SDAD format invalid");
        emv_offline_auth_set_tvr(false, false);
        return EMV_ERR_BAD_DATA;
    }

    // 验证SDAD的摘要
    ret = _verify_sdad_hash(recovered_data, sdad_len, dynamic_data, dynamic_data_len);
    if (ret != EMV_OK)
    {
        EmvLog("_verify_sdad_hash failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 提取ICC Dynamic Data中的ICC动态数(9F4C)
    // recovered_data[3] = Ldd, recovered_data[4] = ICC Dynamic Number Length
    {
        unsigned char ldd = recovered_data[3];
        unsigned char idn_len = recovered_data[4];

        if (ldd >= (1U + idn_len) && idn_len > 0 && (4U + ldd) <= (sdad_len - 21U))
        {
            emv_tlv_set(EMV_TAG_ICC_DYNAMIC_NUMBER, recovered_data + 5U, idn_len);
        }
    }

    g_emv_session.tvr.data_verify_result.not_executed = 0;
    g_emv_session.tvr.data_verify_result.dda_failed = 0;

    return EMV_OK;
}
