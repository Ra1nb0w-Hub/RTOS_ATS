#include "ats_reader.h"
#include "ats_error.h"
#include "emv_lib/include/emv_api.h"
#include "emv_lib/include/emv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define EMV_PCSC_WINDOWS 1
#include <windows.h>
#if defined(__has_include)
#if __has_include(<winscard.h>)
#include <winscard.h>
#define EMV_PCSC_HAS_WINSCARD 1
#endif
#endif
#ifndef EMV_PCSC_HAS_WINSCARD
#undef EMV_PCSC_WINDOWS
#endif
#endif

#ifndef EMV_PCSC_WINDOWS
#undef EMV_PCSC_HAS_WINSCARD
#endif

#ifndef EMV_PCSC_SETTLE_INTERVAL_MS
#define EMV_PCSC_SETTLE_INTERVAL_MS 100U
#endif

#ifndef EMV_PCSC_SETTLE_CHECK_COUNT
#define EMV_PCSC_SETTLE_CHECK_COUNT 1U
#endif

#ifndef EMV_PCSC_POWER_ON_SETTLE_INTERVAL_MS
#define EMV_PCSC_POWER_ON_SETTLE_INTERVAL_MS 100U
#endif

#ifndef EMV_PCSC_POWER_ON_SETTLE_CHECK_COUNT
#define EMV_PCSC_POWER_ON_SETTLE_CHECK_COUNT 1U
#endif

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
#endif

typedef struct EMVPcscContext {
#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    SCARDCONTEXT context;
    SCARDHANDLE card_handle;
    DWORD active_protocol;
    char reader_name[256];
#endif
    unsigned int last_hw_error;
    bool is_init;
    bool is_open;
    bool is_connected;
} EMVPcscContext;

static EMVPcscContext g_pcsc_context;

#if !defined(EMV_PCSC_WINDOWS) || !defined(EMV_PCSC_HAS_WINSCARD)
typedef int SCARD_IO_REQUEST;
#endif

/**
 * @brief 释放当前 PC/SC 卡连接。
 *
 * @param ctx PC/SC 上下文。
 */
static void emv_pcsc_disconnect_card(EMVPcscContext *ctx)
{
#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    if (ctx && ctx->is_connected) {
        SCardDisconnect(ctx->card_handle, SCARD_LEAVE_CARD);
        ctx->card_handle = 0;
        ctx->active_protocol = 0;
        ctx->is_connected = false;
    }
#else
    (void)ctx;
#endif
}

/**
 * @brief 确认卡片在一段时间内持续保持 PRESENT，避免插卡瞬态抖动。
 *
 * @param ctx PC/SC 上下文。
 *
 * @return EMV_OK 表示稳定存在，否则返回错误码。
 */
static int emv_pcsc_confirm_present_stable(EMVPcscContext *ctx)
{
#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    LONG rc = 0;
    SCARD_READERSTATEA state;
    unsigned int idx = 0;

    if (!ctx || !ctx->is_open || ctx->reader_name[0] == '\0') {
        return EMV_ERR_BAD_STATE;
    }

    for (idx = 0; idx < EMV_PCSC_SETTLE_CHECK_COUNT; ++idx) {
        Sleep((DWORD)EMV_PCSC_SETTLE_INTERVAL_MS);

        memset(&state, 0, sizeof(state));
        state.szReader = ctx->reader_name;
        state.dwCurrentState = SCARD_STATE_UNAWARE;

        rc = SCardGetStatusChangeA(ctx->context, 0, &state, 1);
        if (rc != SCARD_S_SUCCESS) {
            ctx->last_hw_error = (unsigned int)rc;
            return EMV_ERR_READER_POLL;
        }
        if ((state.dwEventState & SCARD_STATE_PRESENT) == 0) {
            return EMV_ERR_READER_NOT_FOUND_CARD;
        }
    }

    return EMV_OK;
#else
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 上电连接后连续确认卡状态稳定，降低热插卡后首条 APDU 失败概率。
 *
 * @param ctx PC/SC 上下文。
 * @param protocol_out 输出协议。
 * @param atr_out 输出 ATR 缓冲区。
 * @param atr_out_len 输入时为缓冲区大小，输出时为 ATR 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_confirm_power_on_stable(EMVPcscContext *ctx, DWORD *protocol_out, BYTE *atr_out, DWORD *atr_out_len)
{
#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    LONG rc = 0;
    DWORD idx = 0;
    DWORD reader_len = 0;
    DWORD state = 0;
    DWORD protocol = 0;
    BYTE atr_buf[64];
    DWORD atr_buf_len = 0;

    if (!ctx || !ctx->is_connected || !protocol_out || !atr_out || !atr_out_len) {
        return EMV_ERR_BAD_STATE;
    }

    for (idx = 0; idx < EMV_PCSC_POWER_ON_SETTLE_CHECK_COUNT; ++idx) {
        if (idx > 0) {
            Sleep((DWORD)EMV_PCSC_POWER_ON_SETTLE_INTERVAL_MS);
        }

        atr_buf_len = (DWORD)sizeof(atr_buf);
        reader_len = 0;
        state = 0;
        protocol = 0;
        rc = SCardStatusA(ctx->card_handle,
                          NULL,
                          &reader_len,
                          &state,
                          &protocol,
                          atr_buf,
                          &atr_buf_len);
        if (rc != SCARD_S_SUCCESS) {
            ctx->last_hw_error = (unsigned int)rc;
            return EMV_ERR_READER_IO;
        }
        if ((state & SCARD_PRESENT) == 0) {
            return EMV_ERR_READER_NOT_FOUND_CARD;
        }
        if ((protocol & (SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1)) == 0) {
            return EMV_ERR_READER_IO;
        }
    }

    if (*atr_out_len < atr_buf_len) {
        *atr_out_len = atr_buf_len;
        return EMV_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(atr_out, atr_buf, atr_buf_len);
    *atr_out_len = atr_buf_len;
    *protocol_out = protocol;
    return EMV_OK;
#else
    (void)ctx;
    (void)protocol_out;
    (void)atr_out;
    (void)atr_out_len;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 根据协议选择 `SCardTransmit` 所需的 PCI。
 *
 * @param protocol 当前连接协议。
 *
 * @return 成功时返回 PCI 指针，否则返回 NULL。
 */
static const SCARD_IO_REQUEST *emv_pcsc_get_pci(unsigned long protocol)
{
#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    if (protocol == SCARD_PROTOCOL_T0) {
        return SCARD_PCI_T0;
    }
    if (protocol == SCARD_PROTOCOL_T1) {
        return SCARD_PCI_T1;
    }
#else
    (void)protocol;
#endif
    return NULL;
}

/**
 * @brief 初始化 PC/SC 上下文。
 *
 * @param reader_if 读卡器接口。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_init(void)
{
    memset(&g_pcsc_context, 0, sizeof(g_pcsc_context));
    g_pcsc_context.is_init = true;

    return EMV_OK;
}

/**
 * @brief 打开 PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_open(void)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    if (!ctx->is_open) {
        LONG rc = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx->context);
        if (rc != SCARD_S_SUCCESS) {
            ctx->last_hw_error = (unsigned int)rc;
            return EMV_ERR_READER_OPEN;
        }
        ctx->is_open = true;
    }
    return EMV_OK;
#else
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 关闭 PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_close(void)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    emv_pcsc_disconnect_card(ctx);
    if (ctx->is_open) {
        SCardReleaseContext(ctx->context);
        ctx->context = 0;
        ctx->is_open = false;
    }
    return EMV_OK;
#else
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 轮询当前 PC/SC 读卡器状态。
 *
 * @param card_interface 输出的卡片接口。
 * @param timeout_ms 超时时间，单位毫秒。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_poll(EMVInterfaceType *card_interface, unsigned int timeout_ms)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx || !card_interface) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    LONG rc = 0;
    int confirm_ret = EMV_OK;
    bool wait_timed_out = false;
    DWORD reader_name_len = (DWORD)sizeof(ctx->reader_name);
    SCARD_READERSTATEA state;
    char reader_list[512];
    DWORD reader_list_len = (DWORD)sizeof(reader_list);

    *card_interface = EMV_INTERFACE_NONE;
    memset(reader_list, 0, sizeof(reader_list));

    if (!ctx->is_open) {
        return EMV_ERR_BAD_STATE;
    }

    rc = SCardListReadersA(ctx->context, NULL, reader_list, &reader_list_len);
    if (rc != SCARD_S_SUCCESS || reader_list[0] == '\0') {
        ctx->last_hw_error = (unsigned int)rc;
        return EMV_ERR_READER_NOT_FOUND_CARD;
    }

    memset(ctx->reader_name, 0, sizeof(ctx->reader_name));
    if (reader_name_len > reader_list_len) {
        reader_name_len = reader_list_len;
    }
    memcpy(ctx->reader_name, reader_list, reader_name_len);

    memset(&state, 0, sizeof(state));
    state.szReader = ctx->reader_name;
    state.dwCurrentState = SCARD_STATE_UNAWARE;

    // 第一次先做即时探测，兼容“程序启动前已插卡”的场景。
    rc = SCardGetStatusChangeA(ctx->context, 0, &state, 1);
    if (rc != SCARD_S_SUCCESS) {
        ctx->last_hw_error = (unsigned int)rc;
        return EMV_ERR_READER_POLL;
    }

    if ((state.dwEventState & SCARD_STATE_PRESENT) != 0) {
        confirm_ret = emv_pcsc_confirm_present_stable(ctx);
        if (confirm_ret != EMV_OK) {
            return confirm_ret;
        }
        *card_interface = EMV_INTERFACE_CONTACT;
        return EMV_OK;
    }

    // 基于当前状态等待变化，timeout_ms 内插卡会触发返回。
    state.dwCurrentState = state.dwEventState & (~SCARD_STATE_CHANGED);
    rc = SCardGetStatusChangeA(ctx->context, timeout_ms, &state, 1);
    if (rc != SCARD_S_SUCCESS && rc != SCARD_E_TIMEOUT) {
        ctx->last_hw_error = (unsigned int)rc;
        return EMV_ERR_READER_POLL;
    }
    wait_timed_out = (rc == SCARD_E_TIMEOUT);

    if ((state.dwEventState & SCARD_STATE_PRESENT) != 0) {
        confirm_ret = emv_pcsc_confirm_present_stable(ctx);
        if (confirm_ret != EMV_OK) {
            return confirm_ret;
        }
        *card_interface = EMV_INTERFACE_CONTACT;
        return EMV_OK;
    }

    // 等待超时或未捕捉到状态变化时，再探测一次，覆盖部分驱动漏报状态变化的情况。
    memset(&state, 0, sizeof(state));
    state.szReader = ctx->reader_name;
    state.dwCurrentState = SCARD_STATE_UNAWARE;
    rc = SCardGetStatusChangeA(ctx->context, 0, &state, 1);
    if (rc != SCARD_S_SUCCESS) {
        ctx->last_hw_error = (unsigned int)rc;
        return EMV_ERR_READER_POLL;
    }
    if ((state.dwEventState & SCARD_STATE_PRESENT) != 0) {
        confirm_ret = emv_pcsc_confirm_present_stable(ctx);
        if (confirm_ret != EMV_OK) {
            return confirm_ret;
        }
        *card_interface = EMV_INTERFACE_CONTACT;
        return EMV_OK;
    }

    if (wait_timed_out) {
        ctx->last_hw_error = (unsigned int)SCARD_E_TIMEOUT;
        return EMV_ERR_TIMEOUT;
    }
    return EMV_ERR_READER_NOT_FOUND_CARD;
#else
    (void)card_interface;
    (void)timeout_ms;
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 取消 PC/SC 阻塞等待。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_cancel(void)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    if (!ctx->is_open) {
        return EMV_ERR_BAD_STATE;
    }
    if (SCardCancel(ctx->context) != SCARD_S_SUCCESS) {
        return EMV_ERR_READER_IO;
    }
    return EMV_OK;
#else
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 连接卡片并返回 ATR。
 *
 * @param atr 输出 ATR 缓冲区。
 * @param atr_len 输入时为缓冲区大小，输出时为 ATR 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_icc_power_on(unsigned char *atr, size_t *atr_len)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx || !atr_len) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    LONG rc = 0;
    int stable_ret = EMV_OK;
    DWORD protocol = 0;
    BYTE atr_buf[64];
    DWORD atr_buf_len = (DWORD)sizeof(atr_buf);

    if (!ctx->is_open || ctx->reader_name[0] == '\0') {
        return EMV_ERR_BAD_STATE;
    }

    if (ctx->is_connected) {
        rc = SCardReconnect(ctx->card_handle,
                            SCARD_SHARE_SHARED,
                            SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                            SCARD_RESET_CARD,
                            &ctx->active_protocol);

        if (rc != SCARD_S_SUCCESS) {
            emv_pcsc_disconnect_card(ctx);
        }
    }

    if (!ctx->is_connected) {
        rc = SCardConnectA(ctx->context,
                           ctx->reader_name,
                           SCARD_SHARE_SHARED,
                           SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                           &ctx->card_handle,
                           &ctx->active_protocol);

        if (rc != SCARD_S_SUCCESS) {
            ctx->last_hw_error = (unsigned int)rc;
            return EMV_ERR_READER_IO;
        }
        ctx->is_connected = true;
    }

    stable_ret = emv_pcsc_confirm_power_on_stable(ctx, &protocol, atr_buf, &atr_buf_len);
    if (stable_ret != EMV_OK) {
        emv_pcsc_disconnect_card(ctx);
        return stable_ret;
    }

    ctx->active_protocol = protocol;
    if (!atr || *atr_len < atr_buf_len) {
        *atr_len = atr_buf_len;
        return EMV_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(atr, atr_buf, atr_buf_len);
    *atr_len = atr_buf_len;
    return EMV_OK;
#else
    (void)atr;
    (void)atr_len;
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 断开当前卡连接。
 *
 * @param user_data PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_icc_power_off(void)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }

    emv_pcsc_disconnect_card(ctx);
    return EMV_OK;
}

/**
 * @brief 通过 PC/SC 发送接触卡 APDU。
 *
 * @param command 输入 APDU。
 * @param command_len APDU 长度。
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_icc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx || !command || command_len == 0 || !response_len) {
        return EMV_ERR_INVALID_PARAM;
    }

#if defined(EMV_PCSC_WINDOWS) && defined(EMV_PCSC_HAS_WINSCARD)
    LONG rc = 0;
    DWORD recv_len = (DWORD)(response ? *response_len : 0);
    const SCARD_IO_REQUEST *pci = NULL;

    if (!ctx->is_connected) {
        return EMV_ERR_BAD_STATE;
    }

    pci = emv_pcsc_get_pci(ctx->active_protocol);
    if (!pci) {
        return EMV_ERR_NOT_SUPPORTED;
    }

    rc = SCardTransmit(ctx->card_handle,
                       pci,
                       (LPCBYTE)command,
                       (DWORD)command_len,
                       NULL,
                       response,
                       &recv_len);
    if (rc != SCARD_S_SUCCESS) {
        ctx->last_hw_error = (unsigned int)rc;
        return EMV_ERR_READER_IO;
    }

    *response_len = recv_len;
    return EMV_OK;
#else
    (void)command;
    (void)command_len;
    (void)response;
    (void)response_len;
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 激活非接触卡并返回 ATS。
 *
 * 当前骨架先复用接触式连接能力，通过 `SCardStatus` 返回的 ATR/ATS
 * 打通非接触链路，后续可按厂商读卡器特性进一步细化。
 *
 * @param ats 输出 ATS 缓冲区。
 * @param ats_len 输入时为缓冲区大小，输出时为 ATS 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_picc_activate(unsigned char *ats, size_t *ats_len)
{
    (void)ats;
    (void)ats_len;
    return EMV_ERR_NOT_SUPPORTED;
}

/**
 * @brief 去激活非接触卡。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_picc_deactivate(void)
{
    return EMV_ERR_NOT_SUPPORTED;
}

/**
 * @brief 通过 PC/SC 发送非接触卡 APDU。
 *
 * @param command 输入 APDU。
 * @param command_len APDU 长度。
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int ats_reader_picc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len)
{
    (void)command;
    (void)command_len;
    (void)response;
    (void)response_len;
    return EMV_ERR_NOT_SUPPORTED;
}

/**
 * @brief 获取最近一次底层 PC/SC 错误码。
 *
 * @return 最近一次底层错误码。
 */
int ats_reader_get_last_hw_error(void)
{
    EMVPcscContext *ctx = &g_pcsc_context;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }
    return (int)ctx->last_hw_error;
}

#ifdef __cplusplus
}
#endif
