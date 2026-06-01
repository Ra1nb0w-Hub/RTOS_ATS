#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "mbedtls/sha1.h"
#include <string.h>

/**
 * @brief 计算并验证SSAD摘要。
 *
 * @param data SSAD恢复数据。
 * @param data_len SSAD恢复数据长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _verify_ssad_hash(const unsigned char *data, size_t data_len)
{
    int ret = EMV_OK;
    unsigned char hash_data[EMV_SHA1_LEN] = {0};
    const size_t hash_offset = data_len - 21U;
    mbedtls_sha1_context sha1_ctx;
    EMVOfflineAuthRecord *record = NULL;

    if (data == NULL)
    {
        EmvLog("Parameter `data` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (data_len < 21U)
    {
        EmvLog("Parameter `data_len` is invalid");
        return EMV_ERR_INVALID_PARAM;
    }

    if (g_emv_session.offline_auth_record.count == 0)
    {
        EmvLog("Offline auth records is empty");
        return EMV_ERR_BAD_DATA;
    }

    mbedtls_sha1_init(&sha1_ctx);
    ret = mbedtls_sha1_starts_ret(&sha1_ctx);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_starts_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_STARTS;
        goto exit;
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, data + 1U, hash_offset - 1U);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }    

    // 脱机认证记录
    for (size_t i = 0; i < g_emv_session.offline_auth_record.count; ++i)
    {
        record = (EMVOfflineAuthRecord *)g_emv_session.offline_auth_record.items[i];
        if (record->length == 0)
        {
            EmvLog("Offline auth record length is 0");
            return EMV_ERR_BAD_DATA;
        }

        ret = mbedtls_sha1_update_ret(&sha1_ctx, record->data, record->length);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
            return EMV_ERR_MBEDTLS_SHA1_UPDATE;
        }
    }

    // 静态数据认证标签列表
    {
        unsigned char tag_list[EMV_MAX_TAG_VALUE_LEN] = {0};
        size_t tag_list_len = sizeof(tag_list);
        size_t offset = 0;

        if (emv_tlv_get(EMV_TAG_SDA_DATA_TAG_LIST, tag_list, &tag_list_len) != EMV_OK)
        {
            EmvLog("Not found tag `%X`", EMV_TAG_SDA_DATA_TAG_LIST);
            return EMV_ERR_BAD_DATA;
        }

        while (offset < tag_list_len)
        {
            unsigned short tag = 0;
            unsigned char value[EMV_MAX_TAG_VALUE_LEN] = {0};
            size_t value_len = sizeof(value);

            ret = emv_tlv_parse_tag(tag_list, tag_list_len, &offset, &tag);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tlv_parse_tag failed(%d)", ret);
                return ret;
            }

            ret = emv_tlv_get(tag, value, &value_len);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tlv_get failed(%d)", ret);
                return ret;
            }

            ret = mbedtls_sha1_update_ret(&sha1_ctx, value, value_len);
            if (ret != 0)
            {
                EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
                return EMV_ERR_MBEDTLS_SHA1_UPDATE;
            }
        }
    }

    ret = mbedtls_sha1_finish_ret(&sha1_ctx, hash_data);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_finish_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_FINISH;
        goto exit;
    }

    if (memcmp(hash_data, data + hash_offset, EMV_SHA1_LEN) != 0)
    {
        EmvLog("The hash data is inconsistent");
        ret = EMV_ERR_HASH_VERIFY;
        goto exit;
    }

    ret = EMV_OK;
exit:
    mbedtls_sha1_free(&sha1_ctx);
    return ret;
}

/**
 * @brief 静态数据认证
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_sda(void)
{
    int ret = EMV_OK;
    EMVIssuerPublicKeyInfo issuer_info = {0};
    EMVCapk *capk_info = NULL;
    unsigned char ssad[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t ssad_len = sizeof(ssad);
    unsigned char recovered_data[EMV_MAX_MODULUS_LEN] = {0};

    // 获取签名静态应用数据
    ret = emv_tlv_get(EMV_TAG_SSAD, ssad, &ssad_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_SSAD, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 获取公钥相关信息
    {
        unsigned char *rid = g_emv_session.app_parameter->aid;
        unsigned char key_index = 0;
        size_t key_index_len = sizeof(key_index);

        // 获取CA公钥索引
        ret = emv_tlv_get(EMV_TAG_CA_PKI, &key_index, &key_index_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_CA_PKI, ret);
            emv_offline_auth_set_tvr(true, false);
            return ret;
        }

        // 查找CA公钥信息
        capk_info = emv_capk_find(rid, key_index);
        if (capk_info == NULL)
        {
            EmvLog("CAPK not found(RID=%02X%02X%02X%02X%02X, Index=%02X)", rid[0], rid[1], rid[2], rid[3], rid[4], key_index);
            emv_offline_auth_set_tvr(false, false);
            return EMV_ERR_NOT_FOUND;
        }
    }

    // 获取发卡行公钥证书
    issuer_info.cert_len = sizeof(issuer_info.cert);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_CERTIFICATE, issuer_info.cert, &issuer_info.cert_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ISSUER_PUBLIC_KEY_CERTIFICATE, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 获取发卡行公钥指数
    issuer_info.exponent_len = sizeof(issuer_info.exponent);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_EXPONENT, issuer_info.exponent, &issuer_info.exponent_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_ISSUER_PUBLIC_KEY_EXPONENT, ret);
        emv_offline_auth_set_tvr(true, false);
        return ret;
    }

    // 获取发卡行公钥余数
    issuer_info.remainder_len = sizeof(issuer_info.remainder);
    ret = emv_tlv_get(EMV_TAG_ISSUER_PUBLIC_KEY_REMAINDER, issuer_info.remainder, &issuer_info.remainder_len);
    if (ret != EMV_OK)
        issuer_info.remainder_len = 0;

    // 恢复发卡行证书中的公钥信息
    ret = emv_offline_auth_recover_issuer(capk_info, &issuer_info);
    if (ret != EMV_OK)
    {
        EmvLog("recover_issuer_public_key failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 解密签名静态应用数据
    ret = emv_tools_rsa_public_key(issuer_info.modulus, issuer_info.modulus_len, issuer_info.exponent, issuer_info.exponent_len, ssad, ssad_len, recovered_data);
    if (ret != EMV_OK)
    {
        EmvLog("rsa_public_recover failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    // 验证签名静态应用数据格式
    if (recovered_data[0] != EMV_FLAG_HEADER || recovered_data[1] != EMV_FLAG_FORMAT_SSAD || recovered_data[2] != EMV_HASH_ALG_SHA1 || recovered_data[ssad_len - 1U] != EMV_FLAG_TRAILER)
    {
        EmvLog("SSAD format invalid");
        emv_offline_auth_set_tvr(false, false);
        return EMV_ERR_BAD_DATA;
    }

    // 验证签名静态应用数据哈希值
    ret = _verify_ssad_hash(recovered_data, ssad_len);
    if (ret != EMV_OK)
    {
        EmvLog("verify_ssad_hash failed(%d)", ret);
        emv_offline_auth_set_tvr(false, false);
        return ret;
    }

    g_emv_session.tvr.data_verify_result.not_executed = 0;
    g_emv_session.tvr.data_verify_result.sda_failed = 0;
    return EMV_OK;
}
