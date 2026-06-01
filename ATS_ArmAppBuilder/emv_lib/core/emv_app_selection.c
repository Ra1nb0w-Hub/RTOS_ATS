#include "emv_internal.h"
#include "../include/emv_tags.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define EMV_PSE_NAME_CONTACT "1PAY.SYS.DDF01"
#define EMV_PSE_NAME_CONTACTLESS "2PAY.SYS.DDF01"

/**
 * @brief 将候选应用加入会话候选列表。
 * 
 * @param app_parameter 应用参数。
 * @param card_aid 卡片 AID。
 * @param card_aid_len 卡片 AID 长度。
 * @param label 应用标签。
 * @param label_len 标签长度。
 * @param priority_indicator 应用优先指示器。
 */
static void _add_candidate_app(EMVAppParameter *app_parameter, const unsigned char *card_aid, size_t card_aid_len, const unsigned char *label, size_t label_len, unsigned char priority_indicator)
{
    int ret = EMV_OK;
    EMVCandidateApp *candidate_app = NULL;

    if (app_parameter == NULL)
    {
        EmvLog("Parameter `app_parameter` is NULL");
        return;
    }

    if (card_aid == NULL)
    {
        EmvLog("Parameter `card_aid` is NULL");
        return;
    }

    if (card_aid_len == 0 || card_aid_len > EMV_MAX_AID_LEN)
    {
        EmvLog("Parameter `card_aid_len` is invalid(%d)", card_aid_len);
        return;
    }

    if (label == NULL)
    {
        EmvLog("Parameter `label` is NULL");
        return;
    }

    if (label_len == 0)
    {
        EmvLog("Parameter `label_len` is 0");
        return;
    }

    // 检查是否已存在相同 AID 的候选应用
    for (size_t i = 0; i < g_emv_session.candidate_app.count; ++i)
    {
        candidate_app = (EMVCandidateApp *)g_emv_session.candidate_app.items[i];
        if (candidate_app->aid_len == card_aid_len && memcmp(candidate_app->aid, card_aid, card_aid_len) == 0)
            return;
    }
    
    ret = emv_tools_container_init(&g_emv_session.candidate_app, EMV_DEFAULT_CANDIDATE_APP_CAPACITY);
    if (ret != EMV_OK)
    {
        EmvLog("emv_tools_container_init failed(%d)", ret);
        return;
    }

    // 加入候选应用列表
    candidate_app = (EMVCandidateApp *)malloc(sizeof(EMVCandidateApp));
    if (candidate_app == NULL)
    {
        EmvLog("malloc failed(%d bytes)", sizeof(EMVCandidateApp));
        return;
    }

    memset(candidate_app, 0, sizeof(EMVCandidateApp));
    candidate_app->app_parameter = app_parameter;
    memcpy(candidate_app->aid, card_aid, card_aid_len);
    candidate_app->aid_len = card_aid_len;
    if (label_len >= sizeof(candidate_app->label))
        label_len = sizeof(candidate_app->label) - 1;
    memcpy(candidate_app->label, label, label_len);
    candidate_app->label_len = label_len;
    candidate_app->priority_indicator = priority_indicator;

    ret = emv_tools_container_add(&g_emv_session.candidate_app, candidate_app);
    if (ret != EMV_OK)
    {
        if (candidate_app)
            free(candidate_app);

        EmvLog("emv_tools_container_add failed(%d)", ret);
        return;
    }
}

/**
 * @brief 从 PSE 目录记录中提取候选应用。
 * 
 * @param record 目录记录数据。
 * @param record_len 目录记录长度。
 */
static void _collect_candidates_from_pse_record(const unsigned char *record, size_t record_len)
{
    EMVAppParameter *app_parameter = NULL;
    const unsigned char *aid_ptr = NULL;
    size_t aid_len = 0;

    if (!record || record_len == 0)
    {
        return;
    }

    // 查找 0x70(模板)
    if (emv_tlv_find_tag(record, record_len, EMV_TAG_RECORD_TEMPLATE, 1, NULL, NULL) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_RECORD_TEMPLATE);
        return;
    }

    // 查找 0x61(应用模板)
    if (emv_tlv_find_tag(record, record_len, EMV_TAG_APPLICATION_TEMPLATE, 2, NULL, NULL) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_APPLICATION_TEMPLATE);
        return;
    }

    // 查找 0x4F(应用标识符)
    if (emv_tlv_find_tag(record, record_len, EMV_TAG_AID, 3, &aid_ptr, &aid_len) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_AID);
        return;
    }

    // 检查终端是否支持该应用
    app_parameter = emv_app_parameter_match(aid_ptr, aid_len);
    if (app_parameter == NULL)
    {
        EmvLog("Not support this app");
        EmvHexLog("AID", (void *)aid_ptr, aid_len);
        return;
    }

    // 解析应用模板数据
    {
        const unsigned char *label_ptr = NULL;
        size_t label_len = 0;
        const unsigned char *prioroty_indicator_ptr = NULL;

        // 查找 0x50(应用标签)
        if (emv_tlv_find_tag(record, record_len, EMV_TAG_APPLICATION_LABEL, 3, &label_ptr, &label_len) == 0)
        {
            EmvLog("Not found tag `0x%X`", EMV_TAG_APPLICATION_LABEL);
            return;
        }

        // 查找 0x87(应用优先指示器)
        emv_tlv_find_tag(record, record_len, EMV_TAG_APPLICATION_PRIORITY_INDICATOR, 3, &prioroty_indicator_ptr, NULL);

        // 添加候选应用
        _add_candidate_app(app_parameter, aid_ptr, aid_len, label_ptr, label_len, prioroty_indicator_ptr ? prioroty_indicator_ptr[0] : 0);
    }
}

/**
 * @brief 通过 PSE 目录构建候选应用列表。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _build_candidates_from_pse(void)
{
    int ret = EMV_OK;
    unsigned char *pse_name = (unsigned char *)EMV_PSE_NAME_CONTACT;
    size_t pse_name_len = sizeof(EMV_PSE_NAME_CONTACT) - 1;
    unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
    size_t response_len = sizeof(response);
    unsigned char sfi = 0;

    // 采用非接触PSE名称
    if (g_emv_session.interface_type == EMV_INTERFACE_CONTACTLESS)
    {
        pse_name = (unsigned char *)EMV_PSE_NAME_CONTACTLESS;
        pse_name_len = sizeof(EMV_PSE_NAME_CONTACTLESS) - 1;
    }

    EmvLog("Current `pse_name`: %s", pse_name);
    ret = emv_cmd_select(pse_name, pse_name_len, false, response, &response_len);
    if (ret != EMV_OK)
    {
        EmvLog("emv_cmd_select failed(%d)", ret);
        return ret;
    }

    // 获取SFI值
    {
        const unsigned char *value_ptr = NULL;
        size_t value_len = 0;

        ret = emv_tlv_find_tag(response, response_len, EMV_TAG_SFI, 3, &value_ptr, &value_len);
        if (ret == 0)
        {
            EmvLog("Not found tag `0x%X`", EMV_TAG_SFI);
            return EMV_ERR_NOT_FOUND;
        }

        sfi = value_ptr[0];
        EmvLog("Select `%s` success, `sfi`: 0x%02X", pse_name, sfi);
    }

    // 循环读取该SFI支付系统目录的记录
    for (unsigned char record_no = 1; record_no <= 16; ++record_no)
    {
        memset(response, 0, sizeof(response));
        response_len = sizeof(response);
        ret = emv_cmd_read_record(record_no, sfi, response, &response_len);
        if (ret != EMV_OK)
        {
            if (ret == EMV_ERR_NOT_FOUND)
                break;

            EmvLog("emv_cmd_read_record failed(%d)", ret);
            return ret;
        }

        _collect_candidates_from_pse_record(response, response_len);
    }

    return (g_emv_session.candidate_app.count > 0) ? EMV_OK : EMV_ERR_NOT_FOUND;
}

/**
 * @brief 从终端 AID 列表提取候选应用。
 * 
 * @param app_parameter 应用参数。
 * @param record 目录记录数据。
 * @param record_len 目录记录长度。
 */
static void _collect_candidates_from_terminal_aid(EMVAppParameter *app_parameter, const unsigned char *record, size_t record_len)
{
    const unsigned char *aid_ptr = NULL;
    size_t aid_len = 0;
    const unsigned char *label_ptr = NULL;
    size_t label_len = 0;
    const unsigned char *prioroty_indicator_ptr = NULL;

    // 查找 0x84(专用文件名称)
    if (emv_tlv_find_tag(record, record_len, EMV_TAG_DF_NAME, 2, &aid_ptr, &aid_len) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_DF_NAME);
        return;
    }

    // 查找 0x50(应用标签)
    if (emv_tlv_find_tag(record, record_len, EMV_TAG_APPLICATION_LABEL, 3, &label_ptr, &label_len) == 0)
    {
        EmvLog("Not found tag `0x%X`", EMV_TAG_APPLICATION_LABEL);
        return;
    }

    // 查找 0x87(应用优先指示器)
    emv_tlv_find_tag(record, record_len, EMV_TAG_APPLICATION_PRIORITY_INDICATOR, 3, &prioroty_indicator_ptr, NULL);

    // 添加候选应用
    _add_candidate_app(app_parameter, aid_ptr, aid_len, label_ptr, label_len, prioroty_indicator_ptr ? prioroty_indicator_ptr[0] : 0);
}

/**
 * @brief 通过终端 AID 列表构建候选应用（PSE 失败时回退）。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _build_candidates_from_terminal_aid(void)
{
    EMVContainer *container = NULL;
    EMVAppParameter *app_parameter = NULL;
    unsigned char response[512] = {0};
    size_t response_len = 0;
    int ret = EMV_OK;

    container = emv_app_parameter_get_container();
    if (container == NULL)
    {
        EmvLog("emv_app_parameter_get_container failed");
        return EMV_ERR_BAD_DATA;
    }

    for (size_t i = 0; i < container->count; ++i)
    {
        app_parameter = (EMVAppParameter *)container->items[i];

        memset(response, 0, sizeof(response));
        response_len = sizeof(response);
        ret = emv_cmd_select(app_parameter->aid, app_parameter->aid_len, false, response, &response_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_cmd_select failed(%d)", ret);
            continue;
        }

        _collect_candidates_from_terminal_aid(app_parameter, response, response_len);

        // ASI=0表示允许部分匹配，继续用P2=0x02查找更多匹配的应用
        if (app_parameter->asi == 0)
        {
            while (1)
            {
                memset(response, 0, sizeof(response));
                response_len = sizeof(response);
                ret = emv_cmd_select(app_parameter->aid, app_parameter->aid_len, true, response, &response_len);
                if (ret != EMV_OK)
                    break;

                _collect_candidates_from_terminal_aid(app_parameter, response, response_len);
            }
        }
    }

    return (g_emv_session.candidate_app.count > 0) ? EMV_OK : EMV_ERR_NOT_FOUND;
}

/**
 * @brief 根据应用优先指示器对候选应用列表进行排序。
 */
static void _candidate_app_sort_by_priority_indicator(void)
{
    EMVCandidateApp *candidate_app = NULL;

    for (size_t i = 0; i < g_emv_session.candidate_app.count - 1; ++i)
    {
        unsigned char prio_i = 0, prio_j = 0;

        candidate_app = (EMVCandidateApp *)g_emv_session.candidate_app.items[i];
        prio_i = candidate_app->priority_indicator & 0x0F;

        for (size_t j = i + 1; j < g_emv_session.candidate_app.count; ++j)
        {
            candidate_app = (EMVCandidateApp *)g_emv_session.candidate_app.items[j];
            prio_j = candidate_app->priority_indicator & 0x0F;

            // 0 表示未指定优先级，放在最后
            // i 未指定优先级，j 有优先级，交换
            // 优先级数值越小，优先级越高，所以当i的优先级大于j时交换
            if ((prio_i == 0 && prio_j != 0) || (prio_i != 0 && prio_j != 0 && prio_i > prio_j))
            {
                g_emv_session.candidate_app.items[j] = g_emv_session.candidate_app.items[i];
                g_emv_session.candidate_app.items[i] = candidate_app;
            }
        }
    }
}

/**
 * @brief 构建候选应用列表。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _build_candidate_list(void)
{
    int ret = EMV_OK;

    // 先从PSE构建候选应用列表
    ret = _build_candidates_from_pse();
    if (ret != EMV_OK)
    {
        EmvLog("_build_candidates_from_pse failed(%d)", ret);

        // 再从终端AID列表构建候选应用列表
        ret = _build_candidates_from_terminal_aid();
        if (ret != EMV_OK)
        {
            EmvLog("_build_candidates_from_terminal_aid failed(%d)", ret);
            goto exit;
        }

        EmvLog("Candidate list built from terminal AID list(%d)", g_emv_session.candidate_app.count);
    }
    else
    {
        EmvLog("Candidate list built from PSE(%d)", g_emv_session.candidate_app.count);
    }

    ret = EMV_OK;
exit:
    if (ret != EMV_OK)
        emv_tools_container_clear(&g_emv_session.candidate_app); // 清空候选应用列表
    else
        _candidate_app_sort_by_priority_indicator(); // 对候选应用列表进行排序

    return ret;
}

/**
 * @brief 从候选应用列表中选择应用。
 * 
 * @param aid 选中的 AID。
 * @param aid_len 选中的 AID 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int _select_app_from_candidate_list(unsigned char *aid, size_t *aid_len)
{
    int ret = EMV_OK;
    unsigned int selected_index = 0;
    EMVCandidateApp *candidate_app = NULL;

    if (aid == NULL)
    {
        EmvLog("Parameter `aid` is NULL");
        ret = EMV_ERR_INVALID_PARAM;
        goto exit;
    }

    if (aid_len == NULL)
    {
        EmvLog("Parameter `aid_len` is NULL");
        ret = EMV_ERR_INVALID_PARAM;
        goto exit;
    }

    // 如果候选列表存在多个应用，需要让用户选择一个
    if (g_emv_session.candidate_app.count > 1)
    {
        ret = g_emv_terminal.select_app_callback((const EMVCandidateApp *)g_emv_session.candidate_app.items, g_emv_session.candidate_app.count, &selected_index);
        if (ret != EMV_OK) {
            EmvLog("select_app_callback failed(%d)", ret);
            goto exit;
        }

        if (selected_index >= g_emv_session.candidate_app.count)
        {
            EmvLog("Invalid selected index: %d", selected_index);
            ret = EMV_ERR_BAD_DATA;
            goto exit;
        }
    }

    // 检查选中的应用是否有效
    candidate_app = (EMVCandidateApp *)g_emv_session.candidate_app.items[selected_index];
    if (candidate_app == NULL)
    {
        EmvLog("Selected candidate app is invalid", selected_index);
        ret = EMV_ERR_BAD_DATA;
        goto exit;
    }

    // 检查选中的应用是否有配置文件
    if (candidate_app->app_parameter == NULL)
    {
        EmvLog("Selected candidate app app_parameter is invalid");
        ret = EMV_ERR_BAD_DATA;
        goto exit;
    }

    g_emv_session.app_parameter = candidate_app->app_parameter;
    memcpy(aid, candidate_app->aid, candidate_app->aid_len);
    *aid_len = candidate_app->aid_len;
    ret = EMV_OK;
exit:
    emv_tools_container_clear(&g_emv_session.candidate_app); // 清空候选应用列表

    return ret;
}

/**
 * @brief 选择应用。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_app_selection(void)
{
    int ret = EMV_OK;
    unsigned char aid[EMV_MAX_AID_LEN] = {0};
    size_t aid_len = 0;

    // 构建候选应用列表
    ret = _build_candidate_list();
    if (ret != EMV_OK)
    {
        EmvLog("_build_candidate_list failed(%d)", ret);
        return ret;
    }

    // 从候选应用列表中选择应用
    ret = _select_app_from_candidate_list(aid, &aid_len);
    if (ret != EMV_OK)
    {
        EmvLog("_select_app_from_candidate_list failed(%d)", ret);
        return ret;
    }

    {
        unsigned char response[EMV_MAX_TLV_BUFFER_LEN] = {0};
        size_t response_len = sizeof(response);
        const unsigned char *value_ptr = NULL;
        size_t value_len = 0;

        // 执行SELECT命令
        ret = emv_cmd_select(aid, aid_len, false, response, &response_len);
        if (ret != EMV_OK)
        {
            EmvLog("emv_cmd_select failed(%d)", ret);
            return ret;
        }

        // 查找 0x50(应用标签)
        if (emv_tlv_find_tag(response, response_len, EMV_TAG_APPLICATION_LABEL, 3, &value_ptr, &value_len) == 0)
        {
            EmvLog("Not found tag `0x%X`", EMV_TAG_APPLICATION_LABEL);
            return EMV_ERR_NOT_FOUND;
        }
        emv_tlv_set(EMV_TAG_APPLICATION_LABEL, value_ptr, value_len);

        // 查找 0x9F38(PDOL)
        if (emv_tlv_find_tag(response, response_len, EMV_TAG_PDOL, 3, &value_ptr, &value_len) == 1)
            emv_tlv_set(EMV_TAG_PDOL, value_ptr, value_len);
        else
            emv_tlv_set(EMV_TAG_PDOL, NULL, 0);

        // 设置当前选择的应用AID
        emv_tlv_set(EMV_TAG_AID, aid, aid_len);
    }

    return EMV_OK;
}
