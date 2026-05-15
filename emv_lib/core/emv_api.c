#include "emv_internal.h"
#include "../include/emv_tags.h"
#include "../port/emv_reader_pcsc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

EMVSession g_emv_session = {0};
EMVReaderInterface g_emv_reader = {0};
EMVTerminalConfig g_emv_terminal = {0};

/**
 * @brief 向会话日志缓冲区打印一条日志。
 *
 * 如果已注册终端日志回调，该函数也会将日志转发给上层。
 *
 * @param format 格式字符串
 */
void emv_set_log(const char *format, ...)
{
    if (!format)
        return;

    if (g_emv_terminal.log_callback)
    {
        char log_buffer[2048] = {0};
        va_list args;

        va_start(args, format);
        vsnprintf(log_buffer, sizeof(log_buffer), format, args);
        va_end(args);

        g_emv_terminal.log_callback(log_buffer);
    }
}

/**
 * @brief 向会话日志缓冲区打印一条十六进制日志。
 *
 * @param pcTitle 日志标题。
 * @param pvData 待打印数据指针。
 * @param uiDataLen 数据长度。
 */
void emv_set_log_hex(char *pcTitle, void *pvData, unsigned int uiDataLen)
{
    unsigned char *pcInData = (unsigned char *)pvData;
    char pcLogData[2048] = {0};
    unsigned int uiGroupCount = 0, uiOffsetLen = 0, uiRemainLen = 0;

    if (pvData == NULL || uiDataLen == 0)
        return;

    if (pcTitle && pcTitle[0])
        uiOffsetLen += snprintf(pcLogData + uiOffsetLen, 2048 - uiOffsetLen, "%s(%d bytes):\n", pcTitle, uiDataLen);

    uiRemainLen = uiDataLen;
    uiGroupCount = (uiDataLen + 15) / 16;
    for (unsigned int i = 0; i < uiGroupCount && uiOffsetLen < 2045; i++)
    {
        for (int j = 0; j < 16 && uiRemainLen > 0 && uiOffsetLen < 2045; j++)
        {
            uiOffsetLen += snprintf(pcLogData + uiOffsetLen, 2048 - uiOffsetLen, j == 0 ? "%02X" : " %02X", pcInData[i * 16 + j]);
            uiRemainLen--;
        }

        uiOffsetLen += snprintf(pcLogData + uiOffsetLen, 2048 - uiOffsetLen, "\n");
        
    }

    emv_set_log(pcLogData);
}

/**
 * @brief 销毁一个 EMV 会话。
 */
void emv_session_deinit(void)
{
    // 释放会话中的读卡器
    if (g_emv_reader.get_status && g_emv_reader.get_status(g_emv_reader.user_data) && g_emv_reader.close) {
        g_emv_reader.close(g_emv_reader.user_data);
    }

    // 释放会话中的 候选应用列表 占用的内存
    emv_tools_container_clear(&g_emv_session.candidate_app);

    // 释放会话中的 脱机认证记录 占用的内存
    emv_tools_container_clear(&g_emv_session.offline_auth_record);

    // 释放会话中的 TLV 占用的内存
    emv_tlv_clear();

    memset(&g_emv_session, 0, sizeof(EMVSession));
}

/**
 * @brief 创建一个新的 EMV 会话。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_session_init(void)
{
    int ret = EMV_OK;

    emv_session_deinit();

    ret = emv_reader_use_pcsc_driver(&g_emv_reader);
    if (ret != EMV_OK)
    {
        EmvLog("emv_reader_use_pcsc_driver failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 设置终端配置。
 *
 * @param config 配置结构体指针。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_terminal_set_config(const EMVTerminalConfig *config)
{
    if (!config)
        return EMV_ERR_INVALID_PARAM;

    memcpy(&g_emv_terminal, config, sizeof(EMVTerminalConfig));

    // 检查国家代码是否为空
    if (memcmp(g_emv_terminal.country_code, "\x00\x00", 2) == 0)
    {
        EmvLog("Parameter `country_code` is empty");
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查货币代码是否为空
    if (memcmp(g_emv_terminal.currency_code, "\x00\x00", 2) == 0)
    {
        EmvLog("Parameter `currency_code` is empty");
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查终端能力是否为空
    if (memcmp(g_emv_terminal.terminal_capabilities, "\x00\x00\x00", 3) == 0)
    {
        EmvLog("Parameter `terminal_capabilities` is empty");
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查额外终端能力是否为空
    if (memcmp(g_emv_terminal.additional_terminal_capabilities, "\x00\x00\x00\x00\x00", 5) == 0)
    {
        EmvLog("Parameter `additional_terminal_capabilities` is empty");
        return EMV_ERR_INVALID_PARAM;
    }

    // 检查随机数接口是否为空
    if (g_emv_terminal.random_callback == NULL)
    {
        EmvLog("Function `random_callback` missing");
        return EMV_ERR_NOT_FOUND;
    }

    if (g_emv_terminal.detect_card_callback == NULL)
    {
        EmvLog("Function `detect_card_callback` missing");
        return EMV_ERR_NOT_FOUND;
    }

    // 检查候选应用选择接口是否为空
    if (g_emv_terminal.select_app_callback == NULL)
    {
        EmvLog("Function `select_app_callback` missing");
        return EMV_ERR_NOT_FOUND;
    }

    // 检查PIN输入接口是否为空
    if (g_emv_terminal.input_pin_callback == NULL)
    {
        EmvLog("Function `input_pin_callback` missing");
        return EMV_ERR_NOT_FOUND;
    }

    return EMV_OK;
}

/**
 * @brief 获取终端配置。
 *
 * @param config 配置结构体指针。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_terminal_get_config(EMVTerminalConfig *config)
{
    if (!config)
        return EMV_ERR_INVALID_PARAM;

    memcpy(config, &g_emv_terminal, sizeof(EMVTerminalConfig));

    return EMV_OK;
}

/**
 * @brief 开始一笔新的 EMV 交易。
 *
 * @param request 交易请求对象。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_transaction_begin(const EMVTransactionRequest *request)
{
    int ret = EMV_OK;

    if (!request)
    {
        EmvLog("Parameter `request` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    ret = emv_session_init();
    if (ret != EMV_OK)
    {
        EmvLog("emv_session_init failed(%d)", ret);
        return ret;
    }

    memcpy(&g_emv_session.request, request, sizeof(*request));

    emv_tlv_set(EMV_TAG_AMOUNT, g_emv_session.request.amount, sizeof(g_emv_session.request.amount));
    emv_tlv_set(EMV_TAG_AMOUNT_OTHER, g_emv_session.request.amount_other, sizeof(g_emv_session.request.amount_other));
    emv_tlv_set(EMV_TAG_TRANSACTION_DATE, g_emv_session.request.date, sizeof(g_emv_session.request.date));
    emv_tlv_set(EMV_TAG_TRANSACTION_TIME, g_emv_session.request.time, sizeof(g_emv_session.request.time));
    emv_tlv_set(EMV_TAG_TRANSACTION_TYPE, &g_emv_session.request.type, sizeof(g_emv_session.request.type));
    emv_tlv_set(EMV_TAG_TERMINAL_COUNTRY_CODE, g_emv_terminal.country_code, sizeof(g_emv_terminal.country_code));
    emv_tlv_set(EMV_TAG_TRANSACTION_CURRYENCY_CODE, g_emv_terminal.currency_code, sizeof(g_emv_terminal.currency_code));
    emv_tlv_set(EMV_TAG_TERMINAL_TYPE, &g_emv_terminal.terminal_type, sizeof(g_emv_terminal.terminal_type));
    emv_tlv_set(EMV_TAG_TERMINAL_FLOOR_LIMIT, g_emv_terminal.floor_limit, sizeof(g_emv_terminal.floor_limit));

    emv_tools_parse_terminal_capabilities(g_emv_terminal.terminal_capabilities, &g_emv_session.terminal_capabilities);
    emv_tools_parse_additional_terminal_capabilities(g_emv_terminal.additional_terminal_capabilities, &g_emv_session.additional_terminal_capabilities);

    if (g_emv_terminal.random_callback == NULL)
    {
        EmvLog("Function `random_callback` missing");
        return EMV_ERR_NOT_FOUND;
    }

    // 生成不可预知数
    {
        unsigned char random[4] = {0};

        g_emv_terminal.random_callback(random, sizeof(random));
        emv_tlv_set(EMV_TAG_UNPREDICTABLE_NUMBER, random, sizeof(random));
    }

    return EMV_OK;
}

/**
 * @brief 轮询并检测卡片。
 *
 * @return EMV_OK 表示成功，否则返回错误码或特定状态码。
 */
int emv_step_detect_card(void)
{
    int ret = EMV_OK;

    if (!g_emv_reader.open) {
        EmvLog("reader `open` callback missing");
        return EMV_ERR_NOT_SUPPORTED;
    }

    if (!g_emv_reader.close) {
        EmvLog("reader `close` callback missing");
        return EMV_ERR_NOT_SUPPORTED;
    }

    if (!g_emv_reader.get_status) {
        EmvLog("reader `get_status` callback missing");
        return EMV_ERR_NOT_SUPPORTED;
    }

    if (!g_emv_reader.poll_card) {
        EmvLog("reader `poll_card` callback missing");
        return EMV_ERR_NOT_SUPPORTED;
    }

    if (!g_emv_reader.icc_power_on) {
        EmvLog("reader `icc_power_on` callback missing");
        return EMV_ERR_NOT_SUPPORTED;
    }

    // 打开读卡器
    EmvLog("Open reader...");
    ret = g_emv_reader.open(g_emv_reader.user_data);
    if (ret != EMV_OK) {
        EmvLog("reader `open` failed(%d)", ret);
        return EMV_ERR_READER_OPEN;
    }

    // 轮询卡片接口
    EmvLog("Poll card...");
    g_emv_session.interface_type = EMV_INTERFACE_NONE;
    ret = g_emv_reader.poll_card(g_emv_reader.user_data, &g_emv_session.interface_type, g_emv_session.request.check_interface_timeout);
    if (ret != EMV_OK) {
        if (g_emv_reader.get_status(g_emv_reader.user_data))
            g_emv_reader.close(g_emv_reader.user_data);

        EmvLog("reader `poll_card` failed(%d)", ret);
        return EMV_ERR_READER_POLL;
    }

    // 通知上层已完成卡片检测
    g_emv_terminal.detect_card_callback(g_emv_session.interface_type);

    // 接触式卡片激活
    if (g_emv_session.interface_type == EMV_INTERFACE_CONTACT)
    {
        unsigned char atr[64];
        size_t atr_len = sizeof(atr);

        EmvLog("ICC power on...");
        ret = g_emv_reader.icc_power_on(g_emv_reader.user_data, atr, &atr_len);
        if (ret != EMV_OK) {
            if (g_emv_reader.get_status(g_emv_reader.user_data))
                g_emv_reader.close(g_emv_reader.user_data);
            EmvLog("reader `icc_power_on` failed(%d)", ret);
            return EMV_ERR_READER_IO;
        }
    }

    return EMV_OK;
}

/**
 * @brief 执行应用选择步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_select_application(void)
{
    int ret = EMV_OK;

    // 进行应用选择获取应用初始化需要的数据
    ret = emv_app_selection();
    if (ret != EMV_OK) {
        EmvLog("emv_app_selection failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 执行应用初始化步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_init_application(void)
{
    int ret = EMV_OK;

    ret = emv_app_init();
    if (ret != EMV_OK) {
        EmvLog("emv_app_init failed(%d)", ret);
        return ret;
    }

    return EMV_OK;
}

/**
 * @brief 执行脱机认证步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_offline_authenticate(void)
{
    int ret = EMV_OK;

    // 初始化TVR和TSI
    g_emv_session.tvr.data_verify_result.not_executed = 1;
    g_emv_session.tvr.data_verify_result.cda_failed = 0;
    g_emv_session.tvr.data_verify_result.dda_failed = 0;
    g_emv_session.tvr.data_verify_result.sda_failed = 0;
    g_emv_session.tsi.offline_authenticate = 0;

    EmvLog("Terminal cda: %d, dda: %d, sda: %d", g_emv_session.terminal_capabilities.security.cda, g_emv_session.terminal_capabilities.security.dda, g_emv_session.terminal_capabilities.security.sda);
    EmvLog("AIP cda: %d, dda: %d, sda: %d", g_emv_session.aip.cda, g_emv_session.aip.dda, g_emv_session.aip.sda);
    if (!g_emv_session.aip.cda && !g_emv_session.aip.dda && !g_emv_session.aip.sda)
    {
        EmvLog("AIP not need SDA/DDA/CDA");
        ret = EMV_OK;
        goto exit;
    }

    if (g_emv_session.aip.cda && g_emv_session.terminal_capabilities.security.cda)
    {
        // 初始化标签初始值
        {
            unsigned char zeros[20] = {0};

            emv_tlv_set(EMV_TAG_DATA_AUTH_CODE, zeros, 2);
            emv_tlv_set(EMV_TAG_ICC_DYNAMIC_NUMBER, zeros, 8);
            emv_tlv_set(EMV_TAG_MERCHANT_CUSTOM_DATA, zeros, 20);
        }

        ret = emv_offline_auth_cda();
        if (ret != EMV_OK)
        {
            EmvLog("emv_offline_auth_cda failed(%d)", ret);
            goto exit;
        }
    }
    else if (g_emv_session.aip.dda && g_emv_session.terminal_capabilities.security.dda)
    {
        ret = emv_offline_auth_dda();
        if (ret != EMV_OK)
        {
            EmvLog("emv_offline_auth_dda failed(%d)", ret);
            goto exit;
        }

        g_emv_session.tsi.offline_authenticate = 1;
    }
    else if (g_emv_session.aip.sda && g_emv_session.terminal_capabilities.security.sda)
    {
        ret = emv_offline_auth_sda();
        if (ret != EMV_OK)
        {
            EmvLog("emv_offline_auth_sda failed(%d)", ret);
            goto exit;
        }

        g_emv_session.tsi.offline_authenticate = 1;
    }

exit:
    // 更新TVR和TSI
    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return ret;
}

/**
 * @brief 执行处理限制步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_process_restrictions(void)
{
    int ret = EMV_OK;

    ret = emv_process_restrictions();
    if (ret != EMV_OK) {
        EmvLog("emv_process_restrictions failed(%d)", ret);
        goto exit;
    }

exit:
    // 更新TVR和TSI
    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return ret;
}

/**
 * @brief 执行持卡人验证步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_cardholder_verification(void)
{
    int ret = EMV_OK;

    // 初始化TVR和TSI
    g_emv_session.tvr.cardholder_verify_result.cardholder_failed = 0;
    g_emv_session.tsi.cardholder_verification = 0;

    EmvLog("AIP card_holder: %d", g_emv_session.aip.card_holder);
    if (!g_emv_session.aip.card_holder)
    {
        EmvLog("AIP not need cardholder verification");
        ret = EMV_OK;
        goto exit;
    }

    ret = emv_cardholder_verify();
    if (ret != EMV_OK) {
        EmvLog("emv_cardholder_verify failed(%d)", ret);
        goto exit;
    }

exit:
    // 更新TVR、TSI、CVM结果
    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    emv_tlv_set(EMV_TAG_CVM_RESULTS, (const unsigned char *)&g_emv_session.cvmr, sizeof(EMVCvmResults));
    return ret;
}

/**
 * @brief 执行终端风险管理步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_terminal_risk_management(void)
{
    int ret = EMV_OK;

    // 初始化TVR和TSI
    memset(&g_emv_session.tvr.risk_result, 0, sizeof(g_emv_session.tvr.risk_result));
    g_emv_session.tvr.limit_result.new_card = 0;
    g_emv_session.tsi.terminal_risk_management = 0;

    EmvLog("AIP exec_risk: %d", g_emv_session.aip.exec_risk);
    if (!g_emv_session.aip.exec_risk)
    {
        EmvLog("AIP not need terminal risk management");
        ret = EMV_OK;
        goto exit;
    }

    ret = emv_risk_management();
    if (ret != EMV_OK)
    {
        EmvLog("emv_risk_management failed(%d)", ret);
        goto exit;
    }

    g_emv_session.tsi.terminal_risk_management = 1;
    ret = EMV_OK;
exit:
    // 更新TVR和TSI
    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return ret;
}

/**
 * @brief 执行终端行为分析步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_terminal_action_analysis(bool *need_online)
{
    int ret = EMV_OK;

    memset(&g_emv_session.tvr.action_analysis_result, 0, sizeof(g_emv_session.tvr.action_analysis_result));
    g_emv_session.tsi.card_risk_management = 0;

    ret = emv_action_analysis(need_online);
    if (ret != EMV_OK)
    {
        EmvLog("emv_action_analysis failed(%d)", ret);
        goto exit;
    }

    ret = EMV_OK;
exit:
    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return ret;
}

/**
 * @brief 执行交易处理步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_transaction_process(bool *need_online)
{
    int ret = EMV_OK;

    if (need_online == NULL)
    {
        EmvLog("Parameter `need_online` is NULL");
        return EMV_ERR_INVALID_PARAM;
    }

    // 检测卡片
    ret = emv_step_detect_card();
    if (ret != EMV_OK) {
        EmvLog("emv_step_detect_card failed(%d)", ret);
        goto exit;
    }
    EmvLog("Detect Card Complete");

    // 选择应用
    ret = emv_step_select_application();
    if (ret != EMV_OK) {
        EmvLog("emv_step_select_application failed(%d)", ret);
        goto exit;
    }
    EmvLog("Select Application Complete");

    // 初始化应用
    ret = emv_step_init_application();
    if (ret != EMV_OK) {
        EmvLog("emv_step_init_application failed(%d)", ret);
        goto exit;
    }
    EmvLog("Init Application Complete");

    // 执行脱机认证
    ret = emv_step_offline_authenticate();
    if (ret != EMV_OK) {
        EmvLog("emv_step_offline_authenticate failed(%d)", ret);
        goto exit;
    }
    EmvLog("Offline Authenticate Complete");

    // 执行处理限制
    ret = emv_step_process_restrictions();
    if (ret != EMV_OK) {
        EmvLog("emv_step_process_restrictions failed(%d)", ret);
        goto exit;
    }
    EmvLog("Process Restrictions Complete");

    // 执行持卡人认证
    ret = emv_step_cardholder_verification();
    if (ret != EMV_OK) {
        EmvLog("emv_step_cardholder_verification failed(%d)", ret);
        goto exit;
    }
    EmvLog("Cardholder Verification Complete");

    // 执行终端风险管理
    ret = emv_step_terminal_risk_management();
    if (ret != EMV_OK) {
        EmvLog("emv_step_terminal_risk_management failed(%d)", ret);
        goto exit;
    }
    EmvLog("Terminal Risk Management Complete");

    // 执行终端行为分析
    ret = emv_step_terminal_action_analysis(need_online);
    if (ret != EMV_OK) {
        EmvLog("emv_step_terminal_action_analysis failed(%d)", ret);
        goto exit;
    }
    EmvLog("Terminal Action Analysis Complete");

    ret = EMV_OK;
exit:
    return ret;
}

/**
 * @brief 执行联机完成步骤。
 *
 * @param arc 授权响应码(2字节)，可为NULL表示联机失败
 * @param tlv 服务器返回的TLV数据(可能包含Tag 91/71/72等)
 * @param tlv_len TLV数据长度
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_transaction_online_complete(const char *arc, const unsigned char *tlv, unsigned int tlv_len)
{
    int ret = EMV_OK;

    ret = emv_online_complete(arc, tlv, (size_t)tlv_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_online_complete failed(%d)", ret);
    }

    emv_tools_save_tvr(&g_emv_session.tvr);
    emv_tools_save_tsi(&g_emv_session.tsi);
    return ret;
}

/**
 * @brief 执行交易结束步骤。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_step_transaction_end(void)
{
    emv_session_deinit();
    return EMV_OK;
}
