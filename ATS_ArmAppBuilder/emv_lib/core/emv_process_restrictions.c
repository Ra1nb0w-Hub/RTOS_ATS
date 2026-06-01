#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>
#include <stdbool.h>

/**
 * @brief 检查应用版本。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _check_application_version(void)
{
    unsigned char card_version[2] = {0};
    size_t card_version_len = sizeof(card_version);
    int ret = EMV_OK;

    if (!g_emv_session.app_parameter)
    {
        EmvLog("selected app parameter is missing");
        g_emv_session.tvr.limit_result.version_inconformity = 1;
        return EMV_OK;
    }

    ret = emv_tlv_get(EMV_TAG_APPLICATION_VERSION_NUMBER_CARD, card_version, &card_version_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_APPLICATION_VERSION_NUMBER_CARD, ret);
        g_emv_session.tvr.limit_result.version_inconformity = 1;
        return EMV_OK;
    }

    if (memcmp(card_version, g_emv_session.app_parameter->app_version, sizeof(card_version)) != 0)
    {
        EmvLog("app version mismatch, card=0x%02X%02X terminal=0x%02X%02X",
               card_version[0], card_version[1],
               g_emv_session.app_parameter->app_version[0],
               g_emv_session.app_parameter->app_version[1]);
        g_emv_session.tvr.limit_result.version_inconformity = 1;
        return EMV_OK;
    }

    g_emv_session.tvr.limit_result.version_inconformity = 0;
    return EMV_OK;
}

/**
 * @brief 检查应用日期。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _check_application_date(void)
{
    unsigned char date[3] = {0};
    size_t date_len = sizeof(date);
    const unsigned char *txn_date = g_emv_session.request.date;
    int ret = EMV_OK;

    if (memcmp(txn_date, "\x00\x00\x00", 3) == 0)
    {
        EmvLog("transaction date is missing");
        return EMV_OK;
    }

    // 检查应用过期日期
    ret = emv_tlv_get(EMV_TAG_APPLICATION_EXPIRATION_DATE, date, &date_len);
    if (ret == EMV_OK)
    {
        if (memcmp(txn_date, date, 3) > 0)
        {
            EmvLog("application expired, txn=%02X%02X%02X expiry=%02X%02X%02X",
                    txn_date[0], txn_date[1], txn_date[2], date[0], date[1], date[2]);
            g_emv_session.tvr.limit_result.app_expired = 1;
        }
    }
    else
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_APPLICATION_EXPIRATION_DATE, ret);
    }

    // 检查应用生效日期
    date_len = sizeof(date);
    ret = emv_tlv_get(EMV_TAG_APPLICATION_EFFECTIVE_DATE, date, &date_len);
    if (ret == EMV_OK)
    {
        if (memcmp(txn_date, date, 3) < 0)
        {
            EmvLog("application not yet effective, txn=%02X%02X%02X effective=%02X%02X%02X",
                   txn_date[0], txn_date[1], txn_date[2], date[0], date[1], date[2]);
            g_emv_session.tvr.limit_result.app_effective = 1;
        }
    }
    else
    {
        EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_APPLICATION_EFFECTIVE_DATE, ret);
    }

    return EMV_OK;
}

/**
 * @brief 检查是否为国内交易。
 *
 * @return true 表示是国内交易，否则返回 false。
 */
static bool _is_domestic_transaction(void)
{
    unsigned char issuer_country[2] = {0};
    size_t issuer_country_len = sizeof(issuer_country);
    int ret = EMV_OK;

    ret = emv_tlv_get(EMV_TAG_ISSUER_COUNTRY_CODE, issuer_country, &issuer_country_len);
    if (ret != EMV_OK)
    {
        EmvLog("cannot read issuer country code(5F28), ret=%d", ret);
        return true;
    }

    return (memcmp(issuer_country, g_emv_terminal.country_code, sizeof(issuer_country)) == 0);
}

/**
 * @brief 检查是否为 ATM 终端。
 *
 * @return true 表示是 ATM 终端，否则返回 false。
 */
static bool _is_atm_terminal(void)
{
    // 终端类型“14”、“15”和“16”具备现金支付功能（附加终端功能，字节 1 中“现金”位 = 1）的被视为自动取款机。所有其他终端类型均不被视为自动取款机。
    if (g_emv_session.additional_terminal_capabilities.trans_type1.cash == 1)
    {
        switch (g_emv_terminal.terminal_type)
        {
            case 0x14:
            case 0x15:
            case 0x16:
                return true;
            default:
                return false;
        }
    }

    return false;
}

/**
 * @brief 检查应用使用控制。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _check_application_usage_control(void)
{
    int ret = EMV_OK;
    EMVApplicationUsageControl auc = {0};
    bool is_domestic = true;
    bool allow_service = true;
    bool has_other_amount = false;

    {
        unsigned char data[2] = {0};
        size_t data_len = sizeof(auc);

        ret = emv_tlv_get(EMV_TAG_AUC, data, &data_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_tlv_get `0x%X` failed(%d)", EMV_TAG_AUC, ret);
            return ret;
        }

        emv_tools_parse_auc(data, &auc);
    }

    is_domestic = _is_domestic_transaction();
    has_other_amount = memcmp(g_emv_session.request.amount_other, "\x00\x00\x00\x00\x00\x00", 6) > 0;

    switch (g_emv_session.request.type)
    {
    case EMV_TRANS_PURCHASE:
        allow_service = is_domestic ? auc.domestic_goods : auc.international_goods;
        break;
    case EMV_TRANS_REFUND:
        allow_service = is_domestic ? auc.domestic_service : auc.international_service;
        break;
    case EMV_TRANS_INQUIRY:
        allow_service = is_domestic ? auc.domestic_cash : auc.international_cash;
        break;
    default:
        allow_service = true;
        break;
    }

    if (allow_service && g_emv_session.request.type == EMV_TRANS_INQUIRY)
        allow_service = _is_atm_terminal() ? auc.atm : auc.except_atm;

    if (allow_service && has_other_amount)
        allow_service = is_domestic ? auc.domestic_cash_back : auc.international_cash_back;

    if (!allow_service)
    {
        EmvLog("service is not allowed by AUC, domestic=%d, trans_type=0x%02X", is_domestic, g_emv_session.request.type);
        g_emv_session.tvr.limit_result.unallowed_service = 1;
    }

    return EMV_OK;
}

/**
 * @brief 处理限制。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_process_restrictions(void)
{
    int ret = EMV_OK;

    g_emv_session.tvr.limit_result.version_inconformity = 0;
    g_emv_session.tvr.limit_result.app_expired = 0;
    g_emv_session.tvr.limit_result.app_effective = 0;
    g_emv_session.tvr.limit_result.unallowed_service = 0;

    ret = _check_application_version();
    if (ret != EMV_OK)
    {
        return ret;
    }

    ret = _check_application_date();
    if (ret != EMV_OK)
    {
        return ret;
    }

    ret = _check_application_usage_control();
    if (ret != EMV_OK)
    {
        return ret;
    }

    return EMV_OK;
}
