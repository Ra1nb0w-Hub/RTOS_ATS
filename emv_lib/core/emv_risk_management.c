#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <string.h>

/**
 * @brief 随机百分比(0-99)
 */
static unsigned char _random_percent(void)
{
    unsigned char random_data[2] = {0};

    if (g_emv_terminal.random_callback)
        g_emv_terminal.random_callback(random_data, sizeof(random_data));

    return (unsigned char)((((unsigned int)random_data[0] << 8U) | random_data[1]) % 100U);
}

/**
 * @brief 检查强制联机交易
 */
static void _check_force_online(void)
{
    if (g_emv_session.request.bForceOnline)
    {
        g_emv_session.tvr.risk_result.force_online = 1;
        EmvLog("force online set by transaction request");
    }
}

/**
 * @brief 检查交易金额是否低于终端最低交易限额
 */
static void _check_terminal_floor_limit(void)
{
    size_t floor_limit = 0;
    size_t amount = 0;

    if (g_emv_session.app_parameter)
        floor_limit = emv_tools_hex_to_number(g_emv_session.app_parameter->floor_limit, sizeof(g_emv_session.app_parameter->floor_limit));
    else
        floor_limit = emv_tools_bcd_to_number(g_emv_terminal.floor_limit, sizeof(g_emv_terminal.floor_limit));

    amount = emv_tools_bcd_to_number(g_emv_session.request.amount, sizeof(g_emv_session.request.amount) - 1);

    if (amount >= floor_limit)
    {
        g_emv_session.tvr.risk_result.amount_lower_limit = 1;
        EmvLog("transaction amount >= terminal floor limit(%d, %d)", amount, floor_limit);
    }
}

/**
 * @brief 检查随机选择为联机交易
 */
static void _check_random_online(void)
{
    const EMVAppParameter *app_parameter = g_emv_session.app_parameter;
    size_t amount = 0;
    size_t threshold = 0;
    unsigned char random_percent = 0;
    unsigned char target_percent = 0;

    if (!app_parameter)
        return;

    target_percent = app_parameter->target_percentage;
    if (target_percent == 0)
        return;

    if (target_percent > 99)
        target_percent = 99;

    amount = emv_tools_bcd_to_number(g_emv_session.request.amount, sizeof(g_emv_session.request.amount) - 1);
    threshold = emv_tools_bcd_to_number(app_parameter->threshold, sizeof(app_parameter->threshold));

    if (amount >= threshold && app_parameter->max_target_percentage > target_percent)
    {
        target_percent = app_parameter->max_target_percentage;
        if (target_percent > 99)
            target_percent = 99;
    }

    random_percent = _random_percent();
    if (random_percent < target_percent)
    {
        g_emv_session.tvr.risk_result.random_online = 1;
        EmvLog("transaction selected for random online(%u, %u)", random_percent, target_percent);
    }
}

/**
 * @brief 检查交易频次
 * 
 * 获取连续脱机交易下限和上限
 * 获取上次联机ATC值和当前ATC值
 */
static void _check_velocity(void)
{
    size_t last_online_atc = 0;
    size_t atc = 0;

    // 获取上次联机ATC值和当前ATC值
    {
        unsigned char temp[2] = {0};
        size_t temp_len = sizeof(temp);

        // 获取上次联机ATC值
        if (emv_tlv_get(EMV_TAG_LAST_ONLINE_ATC_REGISTER, temp, &temp_len) != EMV_OK)
        {
            temp_len = sizeof(temp);
            if (emv_cmd_get_data(EMV_TAG_LAST_ONLINE_ATC_REGISTER, temp, &temp_len) != EMV_OK)
            {
                EmvLog("Skip check velocity: Not found tag `0x%X`", EMV_TAG_LAST_ONLINE_ATC_REGISTER);
                g_emv_session.tvr.risk_result.exceed_offline_lower_limit = 1;
                g_emv_session.tvr.risk_result.exceed_offline_upper_limit = 1;
                g_emv_session.tvr.data_verify_result.card_data_missing = 1;
                return;
            }
        }

        last_online_atc = emv_tools_hex_to_number(temp, sizeof(temp));
        EmvLog("`last_online_atc`: %d", last_online_atc);
        if (last_online_atc == 0)
        {
            EmvLog("This is a new card");
            g_emv_session.tvr.limit_result.new_card = 1;
        }

        // 获取当前ATC值
        temp_len = sizeof(temp);
        if (emv_tlv_get(EMV_TAG_ATC, temp, &temp_len) != EMV_OK)
        {
            temp_len = sizeof(temp);
            if (emv_cmd_get_data(EMV_TAG_ATC, temp, &temp_len) != EMV_OK)
            {
                EmvLog("Skip check velocity: Not found tag `0x%X`", EMV_TAG_ATC);
                g_emv_session.tvr.risk_result.exceed_offline_lower_limit = 1;
                g_emv_session.tvr.risk_result.exceed_offline_upper_limit = 1;
                g_emv_session.tvr.data_verify_result.card_data_missing = 1;
                return;
            }
        }

        atc = emv_tools_hex_to_number(temp, sizeof(temp));
        EmvLog("`atc`: %d", atc);

        if (last_online_atc >= atc)
        {
            g_emv_session.tvr.risk_result.exceed_offline_lower_limit = 1;
            g_emv_session.tvr.risk_result.exceed_offline_upper_limit = 1;
            return;
        }
    }

    // 获取连续脱机交易下限和上限
    {
        unsigned char offline_lower_limit = 0;
        unsigned char offline_upper_limit = 0;

        // 获取连续脱机交易下限
        if (emv_tlv_get(EMV_TAG_LOWER_CONSECUTIVE_OFFLINE_LIMIT, &offline_lower_limit, NULL) != EMV_OK)
        {
            EmvLog("Skip check velocity: Not found tag `0x%X`", EMV_TAG_LOWER_CONSECUTIVE_OFFLINE_LIMIT);
            return;
        }

        // 获取连续脱机交易上限
        if (emv_tlv_get(EMV_TAG_UPPER_CONSECUTIVE_OFFLINE_LIMIT, &offline_upper_limit, NULL) != EMV_OK)
        {
            EmvLog("Skip check velocity: Not found tag `0x%X`", EMV_TAG_UPPER_CONSECUTIVE_OFFLINE_LIMIT);
            return;
        }
        // EmvLog("`offline_lower_limit`: %d, `offline_upper_limit`: %d", offline_lower_limit, offline_upper_limit);

        if ((atc - last_online_atc) > offline_lower_limit)
            g_emv_session.tvr.risk_result.exceed_offline_lower_limit = 1;

        if ((atc - last_online_atc) > offline_upper_limit)
            g_emv_session.tvr.risk_result.exceed_offline_upper_limit = 1;
    }
}

/**
 * @brief 终端风险管理
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_risk_management(void)
{
    _check_force_online();
    _check_terminal_floor_limit();
    _check_random_online();
    _check_velocity();

    return EMV_OK;
}
