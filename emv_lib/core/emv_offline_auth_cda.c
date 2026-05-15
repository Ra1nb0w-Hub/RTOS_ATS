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

    g_emv_session.tvr.data_verify_result.not_executed = 0;
    g_emv_session.tvr.data_verify_result.cda_failed = 0;
    return EMV_OK;
}

/**
 * @brief 验证GAC响应中的CDA签名动态应用数据(SDAD)
 *
 * 在GENERATE AC命令返回后调用，使用已恢复的ICC公钥验证SDAD。
 * 按照EMV Book 2 Section 6.6.2执行：
 * 1. RSA解密SDAD
 * 2. 校验格式标记(0x6A/0x05/0x01/0xBC)
 * 3. SHA-1哈希验证
 * 4. 提取并验证CID、AC
 * 5. 提取ICC动态数并保存
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_cda_verify_sdad(void)
{
    int ret = EMV_OK;
    unsigned char sdad[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t sdad_len = sizeof(sdad);
    unsigned char recovered[EMV_MAX_MODULUS_LEN] = {0};
    unsigned char unpredictable_number[4] = {0};
    size_t un_len = sizeof(unpredictable_number);
    mbedtls_sha1_context sha1_ctx;
    unsigned char hash_result[EMV_SHA1_LEN] = {0};

    if (g_emv_session.icc_pk.modulus_len == 0)
    {
        EmvLog("ICC public key not recovered");
        emv_offline_auth_set_tvr(false, false);
        return EMV_ERR_BAD_DATA;
    }

    // 获取SDAD(9F4B)
    ret = emv_tlv_get(EMV_TAG_SDAD, sdad, &sdad_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_SDAD, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // SDAD长度必须等于ICC公钥模数长度
    if (sdad_len != g_emv_session.icc_pk.modulus_len)
    {
        EmvLog("SDAD length(%u) != ICC PK modulus length(%u)", (unsigned int)sdad_len, (unsigned int)g_emv_session.icc_pk.modulus_len);
        emv_offline_auth_set_tvr(false, false);
        return EMV_ERR_BAD_DATA;
    }

    // RSA解密SDAD
    ret = emv_tools_rsa_public_key(g_emv_session.icc_pk.modulus, g_emv_session.icc_pk.modulus_len,
                                    g_emv_session.icc_pk.exponent, g_emv_session.icc_pk.exponent_len,
                                    sdad, sdad_len, recovered);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_rsa_public_key failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 验证格式: Header(0x6A) | Format(0x05) | HashAlgo(0x01) | ... | Trailer(0xBC)
    if (recovered[0] != EMV_FLAG_HEADER || recovered[1] != EMV_FLAG_FORMAT_SDAD ||
        recovered[2] != EMV_HASH_ALG_SHA1 || recovered[sdad_len - 1U] != EMV_FLAG_TRAILER)
    {
        EmvLog("CDA SDAD format invalid(header=%02X, format=%02X, hash=%02X, trailer=%02X)",
               recovered[0], recovered[1], recovered[2], recovered[sdad_len - 1U]);
        emv_offline_auth_set_tvr(false, false);
        return EMV_ERR_BAD_DATA;
    }

    // 获取不可预知数(9F37)
    ret = emv_tlv_get(EMV_TAG_UNPREDICTABLE_NUMBER, unpredictable_number, &un_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_UNPREDICTABLE_NUMBER, ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // SHA-1哈希验证: Hash(recovered[1..NIC-21] || Unpredictable Number)
    {
        const size_t hash_offset = sdad_len - 21U;

        mbedtls_sha1_init(&sha1_ctx);
        ret = mbedtls_sha1_starts_ret(&sha1_ctx);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_starts_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_STARTS;
            goto hash_exit;
        }

        ret = mbedtls_sha1_update_ret(&sha1_ctx, recovered + 1U, hash_offset - 1U);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
            goto hash_exit;
        }

        ret = mbedtls_sha1_update_ret(&sha1_ctx, unpredictable_number, un_len);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
            goto hash_exit;
        }

        ret = mbedtls_sha1_finish_ret(&sha1_ctx, hash_result);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_finish_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_FINISH;
            goto hash_exit;
        }

        if (memcmp(hash_result, recovered + hash_offset, EMV_SHA1_LEN) != 0)
        {
            EmvLog("CDA SDAD hash mismatch");
            ret = EMV_ERR_HASH_VERIFY;
            goto hash_exit;
        }

        ret = EMV_OK;
hash_exit:
        mbedtls_sha1_free(&sha1_ctx);
        if (ret != EMV_OK)
        {
            emv_offline_auth_set_tvr(false, false);
            return ret;
        }
    }

    // 解析ICC Dynamic Data
    // recovered[3] = Ldd (ICC Dynamic Data Length)
    // recovered[4] = ICC Dynamic Number Length
    // recovered[5..5+idn_len-1] = ICC Dynamic Number
    // recovered[5+idn_len] = CID
    // recovered[6+idn_len..13+idn_len] = AC (8 bytes)
    {
        unsigned char ldd = recovered[3];
        unsigned char idn_len = recovered[4];
        size_t dyn_offset = 5U;

        if (ldd < (1U + idn_len + 1U + 8U) || (4U + ldd) > (sdad_len - 21U))
        {
            EmvLog("CDA ICC Dynamic Data length invalid(Ldd=%u, idn_len=%u)", ldd, idn_len);
            emv_offline_auth_set_tvr(false, false);
            return EMV_ERR_BAD_DATA;
        }

        // 保存ICC动态数(9F4C)
        emv_tlv_set(EMV_TAG_ICC_DYNAMIC_NUMBER, recovered + dyn_offset, idn_len);
        dyn_offset += idn_len;

        // 验证CID与GAC响应中的CID一致
        {
            unsigned char gac_cid = 0;
            ret = emv_tlv_get(EMV_TAG_CID, &gac_cid, NULL);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tlv_get CID failed(%d)", ret);
                emv_offline_auth_set_tvr(true, false);
                return ret;
            }

            if (recovered[dyn_offset] != gac_cid)
            {
                EmvLog("CDA CID mismatch(SDAD=%02X, GAC=%02X)", recovered[dyn_offset], gac_cid);
                emv_offline_auth_set_tvr(false, false);
                return EMV_ERR_BAD_DATA;
            }
        }
        dyn_offset += 1U;

        // 保存AC(9F26)
        emv_tlv_set(EMV_TAG_AC, recovered + dyn_offset, 8U);
    }

    // CDA验证成功
    g_emv_session.tvr.data_verify_result.not_executed = 0;
    g_emv_session.tvr.data_verify_result.cda_failed = 0;
    g_emv_session.tsi.offline_authenticate = 1;

    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return EMV_OK;
}
