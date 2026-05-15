#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "mbedtls/sha1.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief 计算并验证发卡行公钥证书摘要。
 *
 * @param cert_recovered 证书恢复结果。
 * @param cert_len 证书长度。
 * @param remainder 发卡行公钥余数(92)。
 * @param remainder_len 余数长度。
 * @param exponent 发卡行公钥指数(9F32)。
 * @param exponent_len 指数长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _verify_issuer_cert_hash(const unsigned char *cert_recovered, size_t cert_len, const unsigned char *remainder, size_t remainder_len, const unsigned char *exponent, size_t exponent_len)
{
    int ret = EMV_OK;
    mbedtls_sha1_context sha1_ctx;
    unsigned char hash_data[EMV_SHA1_LEN] = {0};
    const size_t hash_offset = cert_len - 21U;

    if (!cert_recovered || cert_len < 42U || !exponent || exponent_len == 0)
    {
        return EMV_ERR_INVALID_PARAM;
    }

    mbedtls_sha1_init(&sha1_ctx);
    ret = mbedtls_sha1_starts_ret(&sha1_ctx);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_starts_ret failed(-0x%04x)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_STARTS;
        goto exit;
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, cert_recovered + 1U, hash_offset - 1U);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04x)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    if (remainder && remainder_len > 0)
    {
        ret = mbedtls_sha1_update_ret(&sha1_ctx, remainder, remainder_len);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04x)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
            goto exit;
        }
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, exponent, exponent_len);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04x)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    ret = mbedtls_sha1_finish_ret(&sha1_ctx, hash_data);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_finish_ret failed(-0x%04x)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_FINISH;
        goto exit;
    }

    if (memcmp(hash_data, cert_recovered + hash_offset, EMV_SHA1_LEN) != 0)
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
 * @brief 计算并验证ICC公钥证书摘要。
 *
 * @param cert_recovered ICC公钥证书恢复数据。
 * @param cert_len ICC公钥证书恢复数据长度。
 * @param remainder 余数。
 * @param remainder_len 余数长度。
 * @param exponent 指数。
 * @param exponent_len 指数长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _verify_icc_cert_hash(const unsigned char *cert_recovered, size_t cert_len, const unsigned char *remainder, size_t remainder_len, const unsigned char *exponent, size_t exponent_len)
{
    int ret = EMV_OK;
    mbedtls_sha1_context sha1_ctx;
    unsigned char hash_data[EMV_SHA1_LEN] = {0};
    unsigned char sda_tag_list[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t sda_tag_list_len = sizeof(sda_tag_list);
    EMVOfflineAuthRecord *record = NULL;
    const size_t hash_offset = cert_len - 21U;

    if (!cert_recovered || cert_len < 42U || !exponent || exponent_len == 0)
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

    ret = mbedtls_sha1_update_ret(&sha1_ctx, cert_recovered + 1U, hash_offset - 1U);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    if (remainder && remainder_len > 0)
    {
        ret = mbedtls_sha1_update_ret(&sha1_ctx, remainder, remainder_len);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
            goto exit;
        }
    }

    ret = mbedtls_sha1_update_ret(&sha1_ctx, exponent, exponent_len);
    if (ret != 0)
    {
        EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
        goto exit;
    }

    // 拼接静态认证数据记录
    for (size_t i = 0; i < g_emv_session.offline_auth_record.count; ++i)
    {
        record = (EMVOfflineAuthRecord *)g_emv_session.offline_auth_record.items[i];
        if (!record || record->length == 0)
        {
            EmvLog("Offline auth record invalid(index=%u)", (unsigned int)i);
            return EMV_ERR_BAD_DATA;
        }

        ret = mbedtls_sha1_update_ret(&sha1_ctx, record->data, record->length);
        if (ret != 0)
        {
            EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
            ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
            goto exit;
        }
    }

    // 若存在 9F4A(静态认证数据标签列表)，按列表顺序追加对应 tag 的 value
    ret = emv_tlv_get(EMV_TAG_SDA_DATA_TAG_LIST, sda_tag_list, &sda_tag_list_len);
    if (ret == EMV_OK && sda_tag_list_len > 0)
    {
        size_t offset = 0;

        while (offset < sda_tag_list_len)
        {
            unsigned short tag = 0;
            unsigned char value[EMV_MAX_TAG_VALUE_LEN] = {0};
            size_t value_len = sizeof(value);

            ret = emv_tlv_parse_tag(sda_tag_list, sda_tag_list_len, &offset, &tag);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tlv_parse_tag failed(%d)", ret);
                goto exit;
            }

            ret = emv_tlv_get(tag, value, &value_len);
            if (ret != EMV_OK || value_len == 0)
            {
                EmvLog("emv_tlv_get `%X` failed(%d)", tag, ret);
                goto exit;
            }

            ret = mbedtls_sha1_update_ret(&sha1_ctx, value, value_len);
            if (ret != 0)
            {
                EmvLog("mbedtls_sha1_update_ret failed(-0x%04X)", -ret);
                ret = EMV_ERR_MBEDTLS_SHA1_UPDATE;
                goto exit;
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

    if (memcmp(hash_data, cert_recovered + hash_offset, EMV_SHA1_LEN) != 0)
    {
        EmvLog("ICC certificate hash mismatch");
        ret = EMV_ERR_HASH_VERIFY;
        goto exit;
    }

    ret = EMV_OK;
exit:
    mbedtls_sha1_free(&sha1_ctx);
    return ret;
}

/**
 * @brief 设置脱机认证失败相关 TVR 位。
 * 
 * @param mark_missing_data 是否同时标记“IC卡数据缺失”。
 * @param card_in_blacklist 是否同时标记“卡在黑名单中”。
 */
void emv_offline_auth_set_tvr(bool mark_missing_data, bool card_in_blacklist)
{
    g_emv_session.tvr.data_verify_result.not_executed = 0;

    if (g_emv_session.aip.cda == 1 && g_emv_session.terminal_capabilities.security.cda == 1)
        g_emv_session.tvr.data_verify_result.cda_failed = 1;
    else if (g_emv_session.aip.dda == 1 && g_emv_session.terminal_capabilities.security.dda == 1)
        g_emv_session.tvr.data_verify_result.dda_failed = 1;
    else if (g_emv_session.aip.sda == 1 && g_emv_session.terminal_capabilities.security.sda == 1)
        g_emv_session.tvr.data_verify_result.sda_failed = 1;

    if (mark_missing_data)
        g_emv_session.tvr.data_verify_result.card_data_missing = 1;

    if (card_in_blacklist)
        g_emv_session.tvr.data_verify_result.card_in_blacklist = 1;
}

/**
 * @brief 从CA证书中的公钥恢复发卡行证书中的公钥信息。
 * 
 * @param capk_info CAPK信息。
 * @param issuer_info 发卡行信息。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_recover_issuer(const EMVCapk *capk_info, EMVIssuerPublicKeyInfo *issuer_info)
{
    int ret = EMV_OK;
    unsigned char recovered_data[EMV_MAX_MODULUS_LEN] = {0};
    size_t issuer_part_len = 0;
    size_t issuer_pub_len = 0;
    size_t remain_need = 0;

    if (capk_info == NULL)
    {
        EmvLog("Parameter `capk_info` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (!issuer_info)
    {
        EmvLog("Parameter `issuer_info` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (issuer_info->cert_len != capk_info->modulus_len)
    {
        EmvLog("Issuer certificate length(%u), CA modulus length(%u)", issuer_info->cert_len, capk_info->modulus_len);
        return EMV_ERR_BAD_DATA;
    }

    issuer_part_len = issuer_info->cert_len - 36;

    // 从发卡行证书中恢复发卡行公钥模数
    ret = emv_tools_rsa_public_key(capk_info->modulus, capk_info->modulus_len, capk_info->exponent, capk_info->exponent_len, issuer_info->cert, issuer_info->cert_len, recovered_data);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_rsa_public_key failed(%d)", ret);
        return ret;
    }

    if (recovered_data[0] != EMV_FLAG_HEADER || recovered_data[1] != EMV_FLAG_FORMAT_ISSUER_PK || recovered_data[issuer_info->cert_len - 1] != EMV_FLAG_TRAILER)
    {
        EmvLog("Issuer certificate format invalid");
        return EMV_ERR_BAD_DATA;
    }

    if (recovered_data[11] != EMV_HASH_ALG_SHA1 || recovered_data[12] != EMV_PK_ALG_RSA)
    {
        EmvLog("Unsupported issuer cert algorithm(hash=%02X, pk=%02X)", recovered_data[11], recovered_data[12]);
        return EMV_ERR_NOT_SUPPORTED;
    }

    issuer_pub_len = recovered_data[13];
    if (issuer_pub_len == 0 || issuer_pub_len > EMV_MAX_MODULUS_LEN)
    {
        EmvLog("Issuer public key length invalid(%u)", issuer_pub_len);
        return EMV_ERR_BAD_DATA;
    }

    if (recovered_data[14] != issuer_info->exponent_len)
    {
        EmvLog("Issuer exponent length mismatch(cert=%u, tag=%u)", recovered_data[14], issuer_info->exponent_len);
        return EMV_ERR_BAD_DATA;
    }

    // 验证发卡行标识符与PAN匹配
    {
        unsigned char pan[EMV_MAX_PAN_LEN] = {0};
        size_t pan_len = sizeof(pan);
        const unsigned char *issuer_id = recovered_data + 2;

        if (emv_tlv_get(EMV_TAG_PAN, pan, &pan_len) == EMV_OK && pan_len > 0)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                if (issuer_id[i] == 0xFF)
                    break;

                if (issuer_id[i] != pan[i])
                {
                    EmvLog("Issuer identifier mismatch at byte %u (cert=%02X, pan=%02X)", (unsigned int)i, issuer_id[i], pan[i]);
                    return EMV_ERR_BAD_DATA;
                }
            }
        }
    }

    remain_need = 0;
    if (issuer_pub_len > issuer_part_len)
    {
        remain_need = issuer_pub_len - issuer_part_len;
        if (issuer_info->remainder_len != remain_need)
        {
            EmvLog("Issuer remainder length invalid(expect=%u, actual=%u)", remain_need, issuer_info->remainder_len);
            return EMV_ERR_BAD_DATA;
        }
    }

    // 验证发卡行证书哈希值
    ret = _verify_issuer_cert_hash(recovered_data, issuer_info->cert_len, issuer_info->remainder, remain_need, issuer_info->exponent, issuer_info->exponent_len);
    if (ret != EMV_OK)
    {
        EmvLog("_verify_issuer_cert_hash failed(%d)", ret);
        return ret;
    }

    // 提取发卡行公钥模数
    memset(issuer_info->modulus, 0, EMV_MAX_MODULUS_LEN);
    if (issuer_pub_len <= issuer_part_len)
    {
        memcpy(issuer_info->modulus, recovered_data + 15, issuer_pub_len);
    }
    else
    {
        memcpy(issuer_info->modulus, recovered_data + 15, issuer_part_len);
        memcpy(issuer_info->modulus + issuer_part_len, issuer_info->remainder, remain_need);
    }
    issuer_info->modulus_len = issuer_pub_len;

    return EMV_OK;
}

/**
 * @brief 从发卡行证书中的公钥恢复ICC证书中的公钥信息。
 *
 * @param issuer_info 发卡行信息。
 * @param icc_pk ICC信息。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_recover_icc(const EMVIssuerPublicKeyInfo *issuer_info, EMVIccPublicKeyInfo *icc_pk)
{
    int ret = EMV_OK;
    unsigned char recovered_data[EMV_MAX_MODULUS_LEN] = {0};
    size_t icc_part_len = 0;
    size_t icc_pub_len = 0;
    size_t remain_need = 0;

    if (!issuer_info || !icc_pk)
    {
        return EMV_ERR_INVALID_PARAM;
    }

    if (icc_pk->cert_len != issuer_info->modulus_len)
    {
        EmvLog("ICC certificate length(%u), Issuer modulus length(%u)", (unsigned int)icc_pk->cert_len, (unsigned int)issuer_info->modulus_len);
        return EMV_ERR_BAD_DATA;
    }

    if (icc_pk->cert_len <= 42U)
    {
        EmvLog("ICC certificate length invalid(%u)", (unsigned int)icc_pk->cert_len);
        return EMV_ERR_BAD_DATA;
    }

    icc_part_len = icc_pk->cert_len - 42U;
    ret = emv_tools_rsa_public_key(issuer_info->modulus, issuer_info->modulus_len, issuer_info->exponent, issuer_info->exponent_len, icc_pk->cert, icc_pk->cert_len, recovered_data);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_rsa_public_key failed(%d)", ret);
        return ret;
    }

    if (recovered_data[0] != EMV_FLAG_HEADER || recovered_data[1] != EMV_FLAG_FORMAT_ICC_PK || recovered_data[icc_pk->cert_len - 1U] != EMV_FLAG_TRAILER)
    {
        EmvLog("ICC certificate format invalid");
        return EMV_ERR_BAD_DATA;
    }

    if (recovered_data[17] != EMV_HASH_ALG_SHA1 || recovered_data[18] != EMV_PK_ALG_RSA)
    {
        EmvLog("Unsupported ICC cert algorithm(hash=%02X, pk=%02X)", recovered_data[17], recovered_data[18]);
        return EMV_ERR_NOT_SUPPORTED;
    }

    icc_pub_len = recovered_data[19];
    if (icc_pub_len == 0 || icc_pub_len > EMV_MAX_MODULUS_LEN)
    {
        EmvLog("ICC public key length invalid(%u)", (unsigned int)icc_pub_len);
        return EMV_ERR_BAD_DATA;
    }

    if (recovered_data[20] != icc_pk->exponent_len)
    {
        EmvLog("ICC exponent length mismatch(cert=%u, tag=%u)", recovered_data[20], (unsigned int)icc_pk->exponent_len);
        return EMV_ERR_BAD_DATA;
    }

    // 验证ICC证书中的PAN与卡片PAN匹配
    {
        unsigned char pan[EMV_MAX_PAN_LEN] = {0};
        size_t pan_len = sizeof(pan);
        const unsigned char *cert_pan = recovered_data + 2;

        if (emv_tlv_get(EMV_TAG_PAN, pan, &pan_len) == EMV_OK && pan_len > 0)
        {
            unsigned char padded_pan[10];
            memset(padded_pan, 0xFF, sizeof(padded_pan));
            memcpy(padded_pan, pan, pan_len > 10 ? 10 : pan_len);

            if (memcmp(cert_pan, padded_pan, 10) != 0)
            {
                EmvLog("ICC certificate PAN mismatch");
                return EMV_ERR_BAD_DATA;
            }
        }
    }

    remain_need = 0;
    if (icc_pub_len > icc_part_len)
    {
        remain_need = icc_pub_len - icc_part_len;
        if (icc_pk->remainder_len != remain_need)
        {
            EmvLog("ICC remainder length invalid(expect=%u, actual=%u)", (unsigned int)remain_need, (unsigned int)icc_pk->remainder_len);
            return EMV_ERR_BAD_DATA;
        }
    }

    ret = _verify_icc_cert_hash(recovered_data, icc_pk->cert_len, icc_pk->remainder, remain_need, icc_pk->exponent, icc_pk->exponent_len);
    if (ret != EMV_OK)
    {
        EmvLog("_verify_icc_cert_hash failed(%d)", ret);
        return ret;
    }

    memset(icc_pk->modulus, 0, sizeof(icc_pk->modulus));
    if (icc_pub_len <= icc_part_len)
    {
        memcpy(icc_pk->modulus, recovered_data + 21U, icc_pub_len);
    }
    else
    {
        memcpy(icc_pk->modulus, recovered_data + 21U, icc_part_len);
        memcpy(icc_pk->modulus + icc_part_len, icc_pk->remainder, remain_need);
    }
    icc_pk->modulus_len = icc_pub_len;
    return EMV_OK;
}

/**
 * @brief 添加脱机认证记录
 * 
 * @param data 脱机认证记录数据
 * @param length 脱机认证记录数据长度
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_offline_auth_add_record(const unsigned char *data, size_t length)
{
    int ret = EMV_OK;
    EMVOfflineAuthRecord *record = NULL;

    if (!data || length == 0)
    {
        EmvLog("Parameter `data` is NULL, or `length` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (length > EMV_MAX_TAG_VALUE_LEN)
    {
        EmvLog("Parameter `length`:%d is greater than max tag value len:%d", length, EMV_MAX_TAG_VALUE_LEN);
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_tools_container_init(&g_emv_session.offline_auth_record, EMV_DEFAULT_OFFLINE_AUTH_RECORD_CAPACITY);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_init failed(%d)", ret);
        return ret;
    }

    record = (EMVOfflineAuthRecord *)malloc(sizeof(EMVOfflineAuthRecord));
    if (record == NULL)
    {
        EmvLog("malloc failed(%d bytes)", sizeof(EMVOfflineAuthRecord));
        return EMV_ERR_NO_MEMORY;
    }

    memset(record, 0, sizeof(EMVOfflineAuthRecord));
    record->length = length;
    memcpy(record->data, data, length);

    ret = emv_tools_container_add(&g_emv_session.offline_auth_record, record);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_add failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}
