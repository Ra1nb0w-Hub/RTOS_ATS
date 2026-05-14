#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>

#define EMV_CVM_RESULT_UNKNOWN 0U                     // CVM执行结果: 未知
#define EMV_CVM_RESULT_FAILED 1U                      // CVM执行结果: 失败
#define EMV_CVM_RESULT_SUCCESS 2U                     // CVM执行结果: 成功

enum EMVCvmCodeType
{
    TYPE_EXEC_FAILED = 0x00,                          // CVM执行失败

    TYPE_OFFLINE_PLAINTEXT_PIN = 0x01,                // 离线执行明文PIN验证
    TYPE_ONLINE_ENCRYPTED_PIN = 0x02,                 // 联机执行加密PIN验证
    TYPE_OFFLINE_PLAINTEXT_PIN_SIGNATURE = 0x03,      // 离线执行明文PIN验证+签名(纸上)
    TYPE_OFFLINE_ENCRYPTED_PIN = 0x04,                // 离线执行加密PIN验证
    TYPE_OFFLINE_ENCRYPTED_PIN_SIGNATURE = 0x05,      // 离线执行加密PIN验证+签名(纸上)

    TYPE_SIGNATURE = 0x1E,                            // 签名(纸上)
    TYPE_NO_NEED_CVM = 0x1F,                          // 无需CVM
    TYPE_UNKNOWN = 0x3F,                              // 未知类型
};

// 持卡人验证方法(CVM)列表
typedef struct EMVCvmList {
    unsigned int money_x;                             // 金额X
    unsigned int money_y;                             // 金额Y
    EMVCvm cvm[10];                                   // CVM
    size_t cvm_count;                                 // CVM数量
} EMVCvmList;

static int _get_cvm_list_from_tag(EMVCvmList *cvm_list)
{
    int ret = EMV_OK;
    unsigned char tlv[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t tlv_len = sizeof(tlv), offset = 0;
    unsigned char amount[4] = {0};

    if (cvm_list == NULL)
    {
        EmvLog("Parameter `cvm_list` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_tlv_get(EMV_TAG_CVM_LIST, tlv, &tlv_len);
    if (ret != EMV_OK)
    {
        g_emv_session.tvr.data_verify_result.card_data_missing = 1;
        EmvLog("Not found tag `%X`", EMV_TAG_CVM_LIST);
        return ret;
    }

    EmvHexLog("CVM List(0x8E)", tlv, tlv_len);
    if (tlv_len < 10 || (tlv_len - 8) % 2 != 0)
    {
        EmvLog("Invalid data: tag `%X`, length %d", EMV_TAG_CVM_LIST, tlv_len);
        return EMV_ERR_BAD_DATA;
    }

    // 获取金额X和金额Y
    memset(cvm_list, 0, sizeof(EMVCvmList));
    memcpy(amount, tlv, 4);
    cvm_list->money_x = amount[0] << 24 | amount[1] << 16 | amount[2] << 8 | amount[3];
    memcpy(amount, tlv + 4, 4);
    cvm_list->money_y = amount[0] << 24 | amount[1] << 16 | amount[2] << 8 | amount[3];
    offset += 8;

    // 获取CVM数量，最多10个
    cvm_list->cvm_count = (tlv_len - offset) / sizeof(EMVCvm);
    if (cvm_list->cvm_count > 10)
        cvm_list->cvm_count = 10;

    // 获取CVM
    for (size_t i = 0; i < cvm_list->cvm_count; i++)
    {
        memcpy(&cvm_list->cvm[i], tlv + offset, sizeof(EMVCvm));
        offset += sizeof(EMVCvm);
    }

    return EMV_OK;
}

/**
 * @brief 检查是否为`ATM现金交易`
 * 
 * @return true 是为`ATM现金交易`
 * @return false 不为`ATM现金交易`
 */
static bool _is_atm_cash(void)
{
    unsigned char low_nibble = g_emv_terminal.terminal_type & 0x0FU;

    if (low_nibble < 0x04 || low_nibble > 0x06)
        return false;

    if (g_emv_session.additional_terminal_capabilities.trans_type1.cash == 0)
        return false;

    return (g_emv_session.request.type == EMV_TRANS_INQUIRY);
}

/**
 * @brief 检查是否为`有人值守现金交易`
 * 
 * @return true 是为`有人值守现金交易`
 * @return false 不为`有人值守现金交易`
 */
static bool _is_manual_cash(void)
{
    unsigned char low_nibble = g_emv_terminal.terminal_type & 0x0FU;

    if (low_nibble < 0x01 || low_nibble > 0x03)
        return false;

    if (g_emv_session.additional_terminal_capabilities.trans_type1.cash == 0)
        return false;

    return (g_emv_session.request.type == EMV_TRANS_INQUIRY);
}

/**
 * @brief 检查是否为`返现交易`
 * 
 * @return true 是为`返现交易`
 * @return false 不为`返现交易`
 */
static bool _is_cashback(void)
{
    return (memcmp(g_emv_session.request.amount_other, "\x00\x00\x00\x00\x00\x00", 6) != 0);
}

/**
 * @brief 检查CVM验证条件是否满足
 * 
 * @param money_x 金额X
 * @param money_y 金额Y
 * @param condition_code CVM
 * 
 * @return true 满足条件
 * @return false 不满足条件
 */
static bool _check_cvm_condition(unsigned int money_x, unsigned int money_y, unsigned char condition_code)
{
    unsigned int trans_amount = 0;
    unsigned char application_currency_code[2] = {0};

    if (condition_code >= 0x06 && condition_code <= 0x09)
    {
        emv_tlv_get(EMV_TAG_APPLICATION_CURRENCY_CODE, application_currency_code, NULL);

        for (int i = 0; i < 5; ++i)
            trans_amount = trans_amount * 100 + ((g_emv_session.request.amount[i] >> 4) * 10 + (g_emv_session.request.amount[i] & 0x0F));
    }

    switch (condition_code)
    {
    case 0x00:// 总是满足条件
        return true;
    case 0x01:// 如果是`ATM现金交易`
        return _is_atm_cash();
    case 0x02:// 如果不是`ATM现金交易`、`有人值守现金交易`、`返现交易`
        return (!_is_atm_cash() && !_is_manual_cash() && !_is_cashback());
    case 0x03:// 如果是终端支持CVM(先通过终端能力中CVM值判断，后续会根据不同的CVM进行详细判断是否支持该CVM)
        return (g_emv_terminal.terminal_capabilities[1] != 0);
    case 0x04:// 如果是`有人值守现金交易`
        return _is_manual_cash();
    case 0x05:// 如果是`返现交易`
        return _is_cashback();
    case 0x06:// 如果交易货币等于应用货币代码且金额小于`金额X`
        return (memcmp(g_emv_terminal.currency_code, application_currency_code, 2) == 0 && trans_amount < money_x);
    case 0x07:// 如果交易货币等于应用货币代码且金额大于`金额X`
        return (memcmp(g_emv_terminal.currency_code, application_currency_code, 2) == 0 && trans_amount > money_x);
    case 0x08:// 如果交易货币等于应用货币代码且金额小于`金额Y`
        return (memcmp(g_emv_terminal.currency_code, application_currency_code, 2) == 0 && trans_amount < money_y);
    case 0x09:// 如果交易货币等于应用货币代码且金额大于`金额Y`
        return (memcmp(g_emv_terminal.currency_code, application_currency_code, 2) == 0 && trans_amount > money_y);
    default:
        return false;
    }
}

/**
 * @brief 更新CVM结果。
 */
static void _set_cvm_results(unsigned char code, unsigned char condition_code, unsigned char result)
{
    g_emv_session.cvmr.code = code;
    g_emv_session.cvmr.condition_code = condition_code;
    g_emv_session.cvmr.result = result;
}

static int _process_cvm(EMVCvm *cvm)
{
    int ret = EMV_OK;
    EVMCvmCode cvm_code = {0};

    if (cvm == NULL)
    {
        EmvLog("Parameter `cvm` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    emv_tools_parse_cvm_code(cvm->code, &cvm_code);

Retry:
    EmvLog("Current Process CVM(%02X%02X)", cvm->code, cvm->condition_code);
    memset(&g_emv_session.tvr.cardholder_verify_result, 0, sizeof(g_emv_session.tvr.cardholder_verify_result));

    if (cvm_code.type == TYPE_EXEC_FAILED)
    {
        ret = EMV_ERR_CVM_FAILED;
        goto exit;
    }
    else if (cvm_code.type == TYPE_SIGNATURE)
    {
        if (g_emv_session.terminal_capabilities.cvm.paper_signature)
            ret = EMV_OK;
        else
            ret = EMV_ERR_NOT_SUPPORTED;
        goto exit;
    }
    else if (cvm_code.type == TYPE_NO_NEED_CVM)
    {
        if (g_emv_session.terminal_capabilities.cvm.no_cvm)
            ret = EMV_OK;
        else
            ret = EMV_ERR_NOT_SUPPORTED;
        goto exit;
    }
    else if (cvm_code.type >= TYPE_OFFLINE_PLAINTEXT_PIN && cvm_code.type <= TYPE_OFFLINE_ENCRYPTED_PIN_SIGNATURE)
    {
        unsigned char pin_block[8] = {0};
        unsigned char pin_retry_times = 0;

        // 脱机PIN验证前, 先获取PIN重试次数
        if (cvm_code.type != TYPE_ONLINE_ENCRYPTED_PIN)
        {
            // 获取PIN重试次数
            ret = emv_cmd_get_data(EMV_TAG_PIN_TRY_COUNT, &pin_retry_times, NULL);
            if (ret != EMV_OK)
            {
                EmvLog("emv_cmd_get_data `0x%X` failed(%d)", EMV_TAG_PIN_TRY_COUNT, ret);
                return ret;
            }

            // EmvLog("PIN retry times: %d", pin_retry_times);
            if (pin_retry_times == 0)
            {
                EmvLog("CVM(%02X%02X) failed, PIN retry times is 0", cvm->code, cvm->condition_code);
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }
        }
        else
        {
            if (g_emv_session.terminal_capabilities.cvm.online_encrypted_pin == 0)
            {
                EmvLog("CVM(%02X%02X) failed, Not support online encrypted PIN verify", cvm->code, cvm->condition_code);
                ret = EMV_ERR_NOT_SUPPORTED;
                goto exit;
            }
        }

        // 用户输入PIN, 并返回PIN Block
        ret = g_emv_terminal.input_pin_callback(cvm_code.type == TYPE_ONLINE_ENCRYPTED_PIN, (unsigned int)pin_retry_times, pin_block, 8);
        if (ret != EMV_OK)
        {
            EmvLog("input_pin_callback failed(%d)", ret);
            if (ret == EMV_ERR_NOT_INPUT || ret == EMV_ERR_CANCEL)
                g_emv_session.tvr.cardholder_verify_result.not_input_pin = 1;
            else
                g_emv_session.tvr.cardholder_verify_result.pinpad_failed = 1;
            ret = EMV_ERR_CVM_FAILED;
            goto exit;
        }

        // 检查PIN是否为空
        if (pin_block[0] == 0x00)
        {
            EmvLog("CVM(%02X%02X) failed, PIN is empty", cvm->code, cvm->condition_code);
            g_emv_session.tvr.cardholder_verify_result.not_input_pin = 1;
            ret = EMV_ERR_CVM_FAILED;
            goto exit;
        }
        pin_block[0] |= 0x20;

        // 处理明文PIN验证
        if (cvm_code.type == TYPE_OFFLINE_PLAINTEXT_PIN || cvm_code.type == TYPE_OFFLINE_PLAINTEXT_PIN_SIGNATURE)
        {
            if (g_emv_session.terminal_capabilities.cvm.offline_plaintext_pin == 0)
            {
                EmvLog("CVM(%02X%02X) failed, Not support offline plaintext PIN verify", cvm->code, cvm->condition_code);
                ret = EMV_ERR_NOT_SUPPORTED;
                goto exit;
            }

            // 验证明文PIN
            ret = emv_cmd_cardholder_verify(pin_block, 8, true);
            if (ret != EMV_OK)
            {
                EmvLog("emv_cmd_cardholder_verify failed(%d)", ret);
                if (pin_retry_times > 0)
                    goto Retry;

                g_emv_session.tvr.cardholder_verify_result.pin_retry_limit = 1;
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }
        }
        // 处理加密PIN验证
        else if (cvm_code.type == TYPE_OFFLINE_ENCRYPTED_PIN || cvm_code.type == TYPE_OFFLINE_ENCRYPTED_PIN_SIGNATURE)
        {
            unsigned char uncrypted_data[EMV_MAX_MODULUS_LEN] = {0};
            unsigned char encrypted_data[EMV_MAX_MODULUS_LEN] = {0};
            size_t challenge_len = 0;
            unsigned int offset = 0;

            if (g_emv_session.terminal_capabilities.cvm.offline_encrypted_pin == 0)
            {
                EmvLog("CVM(%02X%02X) failed, Not support offline encrypted PIN verify", cvm->code, cvm->condition_code);
                ret = EMV_ERR_NOT_SUPPORTED;
                goto exit;
            }

            if (g_emv_session.icc_pk.modulus_len == 0 || g_emv_session.icc_pk.exponent_len == 0)
            {
                EmvLog("CVM(%02X%02X) failed, ICC public key is empty", cvm->code, cvm->condition_code);
                ret = EMV_ERR_ICC_KEY_INVALID;
                goto exit;
            }

            // 脱机密文标识(1字节)
            uncrypted_data[offset++] = 0x7F;

            // 拷贝PIN Block(8字节)
            memcpy(uncrypted_data + offset, pin_block, 8);
            offset += 8;

            // 从卡片获取挑战码(通常8字节)
            challenge_len = sizeof(uncrypted_data) - offset;
            ret = emv_cmd_get_challenge(uncrypted_data + offset, &challenge_len);
            if (ret != EMV_OK)
            {
                EmvLog("emv_cmd_get_challenge failed(%d)", ret);
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }
            offset += challenge_len;

            // 从终端获取随机数作为填充数据
            if (g_emv_terminal.random_callback)
                g_emv_terminal.random_callback(uncrypted_data + offset, g_emv_session.icc_pk.modulus_len - offset);

            // 使用ICC公钥加密[0x7F + PIN Block + 挑战码 + 随机数]
            ret = emv_tools_rsa_public_key(g_emv_session.icc_pk.modulus, g_emv_session.icc_pk.modulus_len, g_emv_session.icc_pk.exponent, g_emv_session.icc_pk.exponent_len, uncrypted_data, g_emv_session.icc_pk.modulus_len, encrypted_data);
            if (ret != EMV_OK)
            {
                EmvLog("emv_tools_rsa_public_key failed(%d)", ret);
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }

            // 验证加密PIN
            ret = emv_cmd_cardholder_verify(encrypted_data, g_emv_session.icc_pk.modulus_len, false);
            if (ret != EMV_OK)
            {
                EmvLog("emv_cmd_cardholder_verify failed(%d)", ret);
                if (pin_retry_times > 0)
                    goto Retry;

                g_emv_session.tvr.cardholder_verify_result.pin_retry_limit = 1;
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }
        }
        // 处理联机加密PIN
        else if (cvm_code.type == TYPE_ONLINE_ENCRYPTED_PIN)
        {
            g_emv_session.tvr.cardholder_verify_result.input_online_pin = 1;
            ret = EMV_OK;
        }
    }
    else
    {
        EmvLog("CVM(%02X%02X) failed, Not support this CVM", cvm->code, cvm->condition_code);
        ret = EMV_ERR_NOT_SUPPORTED;
        goto exit;
    }

    ret = EMV_OK;
exit:
    return ret;
}

/**
 * @brief 执行持卡人认证。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_cardholder_verify(void)
{
    EMVCvmList cvm_list = {0};
    EVMCvmCode cvm_code = {0};
    int ret = EMV_OK;

    memset(&g_emv_session.tvr.cardholder_verify_result, 0, sizeof(g_emv_session.tvr.cardholder_verify_result));
    _set_cvm_results(TYPE_UNKNOWN, 0x00U, EMV_CVM_RESULT_UNKNOWN);

    // 从Tag中获取CVM列表
    ret = _get_cvm_list_from_tag(&cvm_list);
    if (ret != EMV_OK)
    {
        EmvLog("_get_cvm_list_from_tag failed(%d)", ret);
        return ret;
    }

    // 循环遍历CVM列表
    for (size_t i = 0; i < cvm_list.cvm_count; i++)
    {
        // 检查条件是否满足
        if (_check_cvm_condition(cvm_list.money_x, cvm_list.money_y, cvm_list.cvm[i].condition_code) == false)
        {
            EmvLog("CVM: %02X%02X condition not met", cvm_list.cvm[i].code, cvm_list.cvm[i].condition_code);
            continue;
        }

        ret = _process_cvm(&cvm_list.cvm[i]);
        if (ret != EMV_OK)
        {
            emv_tools_parse_cvm_code(cvm_list.cvm[i].code, &cvm_code);

            if (cvm_code.rule == 0)
            {
                _set_cvm_results(cvm_list.cvm[i].code, cvm_list.cvm[i].condition_code, EMV_CVM_RESULT_FAILED);
                EmvLog("CVM: %02X%02X failed", cvm_list.cvm[i].code, cvm_list.cvm[i].condition_code);
                g_emv_session.tvr.cardholder_verify_result.cardholder_failed = 1;
                ret = EMV_ERR_CVM_FAILED;
                goto exit;
            }
            continue;
        }

        _set_cvm_results(cvm_list.cvm[i].code, cvm_list.cvm[i].condition_code, EMV_CVM_RESULT_SUCCESS);
        EmvLog("CVM: %02X%02X success", cvm_list.cvm[i].code, cvm_list.cvm[i].condition_code);
        ret = EMV_OK;
        break;
    }

exit:
    g_emv_session.tsi.cardholder_verification = 1;
    return ret;
}
