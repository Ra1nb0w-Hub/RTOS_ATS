#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "mbedtls/rsa.h"

#include <string.h>
#include <stdlib.h>

/**
 * @brief 初始化容器。
 * 
 * 首次使用时，容器初始化采用`capacity`的大小进行初始化。
 * 如果容器已满，容器容量会自动扩展为当前容量的2倍。
 * 
 * @param container 容器指针。
 * @param capacity 容器容量。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_container_init(EMVContainer *container, size_t capacity)
{
    void **items = NULL;

    if (container == NULL)
    {
        EmvLog("Parameter `container` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (container->capacity == 0 && capacity == 0)
    {
        EmvLog("Parameter `capacity` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (container->items != NULL)
    {
        // 容器已满，需要扩展容量
        if (container->count == container->capacity)
            capacity = container->capacity * 2;
        else
            return EMV_OK;
    }

    if (container->capacity == 0)
        container->count = 0;

    items = (void **)malloc(capacity * sizeof(void **));
    if (items == NULL)
    {
        EmvLog("malloc failed(capacity: %d, %d bytes)", capacity, capacity * sizeof(void **));
        return EMV_ERR_NO_MEMORY;
    }

    memset(items, 0, capacity * sizeof(void **));

    if (container->items != NULL && container->count > 0)
    {
        memcpy(items, container->items, container->count * sizeof(void **));
        free(container->items);
    }
    
    container->items = items;
    container->capacity = capacity;
    return EMV_OK;
}

/**
 * @brief 向容器添加一个元素。
 * 
 * 元素指针`item`，需要在外部分配内存，容器会负责释放内存。
 * 
 * @param container 容器指针。
 * @param item 元素指针。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_container_add(EMVContainer *container, void *item)
{
    if (container == NULL)
    {
        EmvLog("Parameter `container` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (item == NULL)
    {
        EmvLog("Parameter `item` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (container->capacity == 0)
    {
        int ret = emv_tools_container_init(container, 8);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tools_container_init failed(%d)", ret);
            return ret;
        }
    }

    container->items[container->count++] = item;
    return EMV_OK;
}

/**
 * @brief 清空容器。
 * 
 * 清空容器后，容器容量会重置为0，容器会释放所有已分配的内存。
 * 
 * @param container 容器指针。
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_container_clear(EMVContainer *container)
{
    if (container == NULL)
    {
        EmvLog("Parameter `container` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (container->items != NULL)
    {
        for (size_t i = 0; i < container->count; i++)
        {
            if (container->items[i] != NULL)
            {
                free(container->items[i]);
                container->items[i] = NULL;
            }
        }

        free(container->items);
        container->items = NULL;
    }

    container->count = 0;
    container->capacity = 0;
    return EMV_OK;
}

/**
 * @brief 
 *
 * @param modulus 公钥模数。
 * @param modulus_len 公钥模数长度。
 * @param exponent 公钥指数。
 * @param exponent_len 公钥指数长度。
 * @param input 输入加密/解密数据。
 * @param input_len 输入长度，必须与模长一致。
 * @param output 输出解密/加密数据。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_rsa_public_key(const unsigned char *modulus, size_t modulus_len, const unsigned char *exponent, size_t exponent_len, const unsigned char *input, size_t input_len, unsigned char *output)
{
    int ret = EMV_OK;
    mbedtls_rsa_context rsa_ctx;

    if (modulus == NULL)
    {
        EmvLog("Parameter `modulus` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (modulus_len == 0)
    {
        EmvLog("Parameter `modulus_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (exponent == NULL)
    {
        EmvLog("Parameter `exponent` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (exponent_len == 0)
    {
        EmvLog("Parameter `exponent_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (input == NULL)
    {
        EmvLog("Parameter `input` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (input_len == 0)
    {
        EmvLog("Parameter `input_len` is 0");
        return EMV_ERR_INVALID_PARAM;
    }

    if (input_len != modulus_len)
    {
        EmvLog("Parameter `input_len` is not equal to `modulus_len`");
        return EMV_ERR_INVALID_PARAM;
    }

    if (output == NULL)
    {
        EmvLog("Parameter `output` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    mbedtls_rsa_init(&rsa_ctx, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
    ret = mbedtls_rsa_import_raw(&rsa_ctx, modulus, modulus_len, NULL, 0, NULL, 0, NULL, 0, exponent, exponent_len);
    if (ret != 0)
    {
        EmvLog("mbedtls_rsa_import_raw failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_RSA_IMPORT;
        goto exit;
    }

    ret = mbedtls_rsa_complete(&rsa_ctx);
    if (ret != 0)
    {
        EmvLog("mbedtls_rsa_complete failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_RSA_COMPLETE;
        goto exit;
    }

    ret = mbedtls_rsa_check_pubkey(&rsa_ctx);
    if (ret != 0)
    {
        EmvLog("mbedtls_rsa_check_pubkey failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_RSA_CHECK_PUBKEY;
        goto exit;
    }

    ret = mbedtls_rsa_public(&rsa_ctx, input, output);
    if (ret != 0)
    {
        EmvLog("mbedtls_rsa_public failed(-0x%04X)", -ret);
        ret = EMV_ERR_MBEDTLS_RSA_PUBKEY_DECRYPT;
        goto exit;
    }

    ret = EMV_OK;
exit:
    mbedtls_rsa_free(&rsa_ctx);
    return ret;
}

/**
 * @brief 解析AIP。
 * 
 * @param aip AIP字节序列。
 * @param field 解析结果字段指针。
 */
void emv_tools_parse_aip(const unsigned char aip[2], EMVAppInterchangeProfile *field)
{
    if (aip == NULL || field == NULL)
    {
        EmvLog("Parameter `aip` or `field` is NULL");
        return;
    }

    memset(field, 0, sizeof(EMVAppInterchangeProfile));

    field->sda = (aip[0] & 0x40) ? 1 : 0;
    field->dda = (aip[0] & 0x20) ? 1 : 0;
    field->card_holder = (aip[0] & 0x10) ? 1 : 0;
    field->exec_risk = (aip[0] & 0x08) ? 1 : 0;
    field->card_issuer = (aip[0] & 0x04) ? 1 : 0;
    field->cda = (aip[0] & 0x01) ? 1 : 0;
}

/**
 * @brief 保存TVR。
 * 
 * @param field TVR字段指针。
 */
void emv_tools_save_tvr(EMVTerminalVerificationResults *field)
{
    unsigned char tvr[5] = {0};

    if (field == NULL)
    {
        EmvLog("Parameter `field` is NULL");
        return;
    }

    // 数据验证结果
    tvr[0] |= field->data_verify_result.not_executed << 7;
    tvr[0] |= field->data_verify_result.sda_failed << 6;
    tvr[0] |= field->data_verify_result.card_data_missing << 5;
    tvr[0] |= field->data_verify_result.card_in_blacklist << 4;
    tvr[0] |= field->data_verify_result.dda_failed << 3;
    tvr[0] |= field->data_verify_result.cda_failed << 2;

    // 限制结果
    tvr[1] |= field->limit_result.version_inconformity << 7;
    tvr[1] |= field->limit_result.app_expired << 6;
    tvr[1] |= field->limit_result.app_effective << 5;
    tvr[1] |= field->limit_result.unallowed_service << 4;
    tvr[1] |= field->limit_result.new_card << 3;

    // 持卡人认证结果
    tvr[2] |= field->cardholder_verify_result.cardholder_failed << 7;
    tvr[2] |= field->cardholder_verify_result.unknown_cvm << 6;
    tvr[2] |= field->cardholder_verify_result.pin_retry_limit << 5;
    tvr[2] |= field->cardholder_verify_result.pinpad_failed << 4;
    tvr[2] |= field->cardholder_verify_result.not_input_pin << 3;
    tvr[2] |= field->cardholder_verify_result.input_online_pin << 2;

    // 风险管理结果
    tvr[3] |= field->risk_result.amount_lower_limit << 7;
    tvr[3] |= field->risk_result.exceed_offline_lower_limit << 6;
    tvr[3] |= field->risk_result.exceed_offline_upper_limit << 5;
    tvr[3] |= field->risk_result.random_online << 4;
    tvr[3] |= field->risk_result.force_online << 3;

    // 行为分析结果
    tvr[4] |= field->action_analysis_result.use_default_tdol << 7;
    tvr[4] |= field->action_analysis_result.icc_verify_failed << 6;
    tvr[4] |= field->action_analysis_result.gac_before_failed << 5;
    tvr[4] |= field->action_analysis_result.gac_after_failed << 4;

    emv_tlv_set(EMV_TAG_TVR, (const unsigned char *)&tvr, sizeof(tvr));
}

/**
 * @brief 保存TSI。
 * 
 * @param field TSI字段指针。
 */
void emv_tools_save_tsi(EMVTransactionStatusInfo *field)
{
    unsigned char tsi[2] = {0};

    if (field == NULL)
    {
        EmvLog("Parameter `field` is NULL");
        return;
    }

    tsi[0] |= field->offline_authenticate << 7;
    tsi[0] |= field->cardholder_verification << 6;
    tsi[0] |= field->card_risk_management << 5;
    tsi[0] |= field->issuing_bank_verification << 4;
    tsi[0] |= field->terminal_risk_management << 3;
    tsi[0] |= field->issuing_bank_script << 2;

    emv_tlv_set(EMV_TAG_TSI, (const unsigned char *)&tsi, sizeof(tsi));
}

/**
 * @brief 解析AUC。
 * 
 * @param auc AUC字节序列。
 * @param field 解析结果字段指针。
 */
void emv_tools_parse_auc(const unsigned char auc[2], EMVApplicationUsageControl *field)
{
    if (auc == NULL || field == NULL)
    {
        EmvLog("Parameter `auc` or `field` is NULL");
        return;
    }

    memset(field, 0, sizeof(EMVApplicationUsageControl));
    field->domestic_cash = (auc[0] & 0x80) ? 1 : 0;
    field->international_cash = (auc[0] & 0x40) ? 1 : 0;
    field->domestic_goods = (auc[0] & 0x20) ? 1 : 0;
    field->international_goods = (auc[0] & 0x10) ? 1 : 0;
    field->domestic_service = (auc[0] & 0x08) ? 1 : 0;
    field->international_service = (auc[0] & 0x04) ? 1 : 0;
    field->atm = (auc[0] & 0x02) ? 1 : 0;
    field->except_atm = (auc[0] & 0x01) ? 1 : 0;

    field->domestic_cash_back = (auc[1] & 0x80) ? 1 : 0;
    field->international_cash_back = (auc[1] & 0x40) ? 1 : 0;
}

/**
 * @brief 解析CVM码。
 * 
 * @param cvm_code CVM字节序列。
 * @param field 解析结果字段指针。
 */
void emv_tools_parse_cvm_code(const unsigned char cvm_code, EVMCvmCode *field)
{
    if (field == NULL)
    {
        EmvLog("Parameter `field` is NULL");
        return;
    }

    memset(field, 0, sizeof(EVMCvmCode));
    field->custom = (cvm_code & 0x80) ? 1 : 0;
    field->rule = (cvm_code & 0x40) ? 1 : 0;
    field->type = cvm_code & 0x3F;
}

/**
 * @brief 解析终端能力。
 * 
 * @param terminal_capabilities 终端能力字节序列。
 * @param field 解析结果字段指针。
 */
void emv_tools_parse_terminal_capabilities(const unsigned char terminal_capabilities[3], EMVTerminalCapabilities *field)
{
    if (terminal_capabilities == NULL || field == NULL)
    {
        EmvLog("Parameter `terminal_capabilities` or `field` is NULL");
        return;
    }

    memset(field, 0, sizeof(EMVTerminalCapabilities));
    field->card_data_input.manual_input = (terminal_capabilities[0] & 0x80) ? 1 : 0;
    field->card_data_input.magnetic_stripe = (terminal_capabilities[0] & 0x40) ? 1 : 0;
    field->card_data_input.contact_ic = (terminal_capabilities[0] & 0x20) ? 1 : 0;

    field->cvm.offline_plaintext_pin = (terminal_capabilities[1] & 0x80) ? 1 : 0;
    field->cvm.online_encrypted_pin = (terminal_capabilities[1] & 0x40) ? 1 : 0;
    field->cvm.paper_signature = (terminal_capabilities[1] & 0x20) ? 1 : 0;
    field->cvm.offline_encrypted_pin = (terminal_capabilities[1] & 0x10) ? 1 : 0;
    field->cvm.no_cvm = (terminal_capabilities[1] & 0x08) ? 1 : 0;

    field->security.sda = (terminal_capabilities[2] & 0x80) ? 1 : 0;
    field->security.dda = (terminal_capabilities[2] & 0x40) ? 1 : 0;
    field->security.capture = (terminal_capabilities[2] & 0x20) ? 1 : 0;
    field->security.cda = (terminal_capabilities[2] & 0x08) ? 1 : 0;

    emv_tlv_set(EMV_TAG_TERMINAL_CAPABILITIES, terminal_capabilities, sizeof(EMVTerminalCapabilities));
}

/**
 * @brief 解析额外终端能力。
 * 
 * @param additional_terminal_capabilities 额外终端能力字节序列。
 * @param field 解析结果字段指针。
 */
void emv_tools_parse_additional_terminal_capabilities(const unsigned char additional_terminal_capabilities[5], EMVAdditionalTerminalCapabilities *field)
{
    if (additional_terminal_capabilities == NULL || field == NULL)
    {
        EmvLog("Parameter `additional_terminal_capabilities` or `field` is NULL");
        return;
    }

    memset(field, 0, sizeof(EMVAdditionalTerminalCapabilities));
    field->trans_type1.cash = (additional_terminal_capabilities[0] & 0x80) ? 1 : 0;
    field->trans_type1.goods = (additional_terminal_capabilities[0] & 0x40) ? 1 : 0;
    field->trans_type1.services = (additional_terminal_capabilities[0] & 0x20) ? 1 : 0;
    field->trans_type1.cash_back = (additional_terminal_capabilities[0] & 0x10) ? 1 : 0;
    field->trans_type1.inquiry = (additional_terminal_capabilities[0] & 0x08) ? 1 : 0;
    field->trans_type1.transfer = (additional_terminal_capabilities[0] & 0x04) ? 1 : 0;
    field->trans_type1.payment = (additional_terminal_capabilities[0] & 0x02) ? 1 : 0;
    field->trans_type1.administrative = (additional_terminal_capabilities[0] & 0x01) ? 1 : 0;

    field->trans_type2.cash_deposit = (additional_terminal_capabilities[1] & 0x80) ? 1 : 0;

    field->terminal_data_in.number_keys = (additional_terminal_capabilities[2] & 0x80) ? 1 : 0;
    field->terminal_data_in.alpha_special_keys = (additional_terminal_capabilities[2] & 0x40) ? 1 : 0;
    field->terminal_data_in.command_keys = (additional_terminal_capabilities[2] & 0x20) ? 1 : 0;
    field->terminal_data_in.funtion_keys = (additional_terminal_capabilities[2] & 0x10) ? 1 : 0;

    field->terminal_data_out1.print_to_attendant = (additional_terminal_capabilities[3] & 0x80) ? 1 : 0;
    field->terminal_data_out1.print_to_cardholder = (additional_terminal_capabilities[3] & 0x40) ? 1 : 0;
    field->terminal_data_out1.display_to_attendant = (additional_terminal_capabilities[3] & 0x20) ? 1 : 0;
    field->terminal_data_out1.display_to_cardholder = (additional_terminal_capabilities[3] & 0x10) ? 1 : 0;
    field->terminal_data_out1.code_table_10 = (additional_terminal_capabilities[3] & 0x02) ? 1 : 0;
    field->terminal_data_out1.code_table_9 = (additional_terminal_capabilities[3] & 0x01) ? 1 : 0;

    field->terminal_data_out2.code_table_8 = (additional_terminal_capabilities[4] & 0x80) ? 1 : 0;
    field->terminal_data_out2.code_table_7 = (additional_terminal_capabilities[4] & 0x40) ? 1 : 0;
    field->terminal_data_out2.code_table_6 = (additional_terminal_capabilities[4] & 0x20) ? 1 : 0;
    field->terminal_data_out2.code_table_5 = (additional_terminal_capabilities[4] & 0x10) ? 1 : 0;
    field->terminal_data_out2.code_table_4 = (additional_terminal_capabilities[4] & 0x08) ? 1 : 0;
    field->terminal_data_out2.code_table_3 = (additional_terminal_capabilities[4] & 0x04) ? 1 : 0;
    field->terminal_data_out2.code_table_2 = (additional_terminal_capabilities[4] & 0x02) ? 1 : 0;
    field->terminal_data_out2.code_table_1 = (additional_terminal_capabilities[4] & 0x01) ? 1 : 0;

    emv_tlv_set(EMV_TAG_ADDITIONAL_TERMINAL_CAPABILITIES, additional_terminal_capabilities, sizeof(EMVAdditionalTerminalCapabilities));
}

/**
 * @brief 将十六进制字节序列转换为整数。
 * 
 * @param hex 十六进制字节序列指针。
 * @param hex_len 十六进制字节序列长度。
 * 
 * @return 转换后的整数。
 */
size_t emv_tools_hex_to_number(const unsigned char *hex, size_t hex_len)
{
    size_t value = 0;

    if (hex == NULL || hex_len == 0)
    {
        EmvLog("Parameter `hex` is NULL or `hex_len` is 0");
        return 0;
    }

    for (size_t i = 0; i < hex_len; ++i)
        value = (value << 8) | hex[i];

    return value;
}

size_t emv_tools_bcd_to_number(const unsigned char *bcd, size_t bcd_len)
{
    size_t value = 0;

    if (bcd == NULL || bcd_len == 0)
    {
        EmvLog("Parameter `bcd` is NULL or `bcd_len` is 0");
        return 0;
    }

    for (size_t i = 0; i < bcd_len; ++i)
        value = value * 100 + (bcd[i] >> 4) * 10 + (bcd[i] & 0x0F);

    return value;
}

/**
 * @brief 构建DOL数据。
 * 
 * @param tag DOL标签。
 * @param data 构建后的DOL数据指针。
 * @param data_len 构建后的DOL数据长度指针。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_build_dol_data(unsigned short tag, unsigned char *data, size_t *data_len)
{
    int ret = EMV_OK;
    unsigned char tag_data[EMV_MAX_TAG_VALUE_LEN] = {0};
    size_t tag_data_len = sizeof(tag_data);
    size_t data_capacity = 0;
    size_t offset = 0;

    if (tag != EMV_TAG_CDOL1 && tag != EMV_TAG_CDOL2 && tag != EMV_TAG_PDOL && tag != EMV_TAG_DDOL)
    {
        EmvLog("Parameter `tag` invalid(0x%X)", tag);
        return EMV_ERR_INVALID_PARAM;
    }

    if (data == NULL)
    {
        EmvLog("Parameter `data` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    if (data_len == NULL || *data_len == 0)
    {
        EmvLog("Parameter `data_len` is NULL or 0");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_tlv_get(tag, tag_data, &tag_data_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", tag, ret);
        return ret;
    }

    // EmvHexLog("DOL Tag Data", tag_data, tag_data_len);
    data_capacity = *data_len;
    offset = 0;
    *data_len = 0;
    while (offset < tag_data_len)
    {
        size_t tmp_len1 = 0, tmp_len2 = 0;

        ret = emv_tlv_parse_tag(tag_data, tag_data_len, &offset, &tag);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_tag failed(%d)", ret);
            return ret;
        }

        ret = emv_tlv_parse_length(tag_data, tag_data_len, &offset, &tmp_len1);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_parse_length failed(%d)", ret);
            return ret;
        }

        if (*data_len + tmp_len1 > data_capacity)
        {
            EmvLog("Parameter `data_len` is too small(%d)", data_capacity);
            return EMV_ERR_BUFFER_TOO_SMALL;
        }

        tmp_len2 = data_capacity - *data_len;
        ret = emv_tlv_get(tag, data + *data_len, &tmp_len2);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", tag, ret);
            return ret;
        }

        if (tmp_len1 != tmp_len2)
        {
            EmvLog("DOL data length not match(%d != %d)", tmp_len1, tmp_len2);
            return EMV_ERR_BAD_DATA;
        }

        // EmvLog("DOL add data `0x%X`(%d bytes) successful", tag, tmp_len1);
        *data_len += tmp_len1;
    }

    return EMV_OK;
}

/**
 * @brief 解析CID。
 * 
 * @param cid CID字段值。
 * @param field 解析后的CID字段指针。
 * 
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_tools_parse_cid(const unsigned char cid, EMVCryptogramInformationData *field)
{
    if (field == NULL)
    {
        EmvLog("Parameter `field` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    memset(field, 0, sizeof(EMVCryptogramInformationData));

    field->type = (cid >> 6) & 0xFF;
    field->exist_code = (cid & 0x08) ? 1 : 0;
    field->code = (cid & 0x07) & 0xFF;

    return EMV_OK;
}