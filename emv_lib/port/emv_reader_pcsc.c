#include "emv_reader_pcsc.h"

#include "../include/emv_error.h"
#include "../include/emv_types.h"

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
 * @brief 打开 PC/SC 上下文。
 *
 * @param user_data PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_open(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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
 * @param user_data PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_close(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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

static bool emv_pcsc_get_status(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

    if (!ctx) {
        return false;
    }

    return ctx->is_open && ctx->is_connected;
}

/**
 * @brief 轮询当前 PC/SC 读卡器状态。
 *
 * @param user_data PC/SC 上下文。
 * @param card_interface 输出的卡片接口。
 * @param timeout_ms 超时时间，单位毫秒。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_poll_card(void *user_data, EMVInterfaceType *card_interface, unsigned int timeout_ms)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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
    (void)timeout_ms;
    (void)ctx;
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 取消 PC/SC 阻塞等待。
 *
 * @param user_data PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_cancel_io(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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
 * @param user_data PC/SC 上下文。
 * @param atr 输出 ATR 缓冲区。
 * @param atr_len 输入时为缓冲区大小，输出时为 ATR 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_icc_power_on(void *user_data, unsigned char *atr, size_t *atr_len)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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

    emv_pcsc_disconnect_card(ctx);

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
static int emv_pcsc_icc_power_off(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }

    emv_pcsc_disconnect_card(ctx);
    return EMV_OK;
}

/**
 * @brief 通过 PC/SC 发送接触卡 APDU。
 *
 * @param user_data PC/SC 上下文。
 * @param command 输入 APDU。
 * @param command_len APDU 长度。
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_icc_transceive_apdu(void *user_data,
                                        const unsigned char *command,
                                        size_t command_len,
                                        unsigned char *response,
                                        size_t *response_len)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

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
    return EMV_ERR_NOT_SUPPORTED;
#endif
}

/**
 * @brief 激活非接触卡并返回 ATS。
 *
 * 当前骨架先复用接触式连接能力，通过 `SCardStatus` 返回的 ATR/ATS
 * 打通非接触链路，后续可按厂商读卡器特性进一步细化。
 *
 * @param user_data PC/SC 上下文。
 * @param ats 输出 ATS 缓冲区。
 * @param ats_len 输入时为缓冲区大小，输出时为 ATS 长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_picc_activate(void *user_data, unsigned char *ats, size_t *ats_len)
{
    return emv_pcsc_icc_power_on(user_data, ats, ats_len);
}

/**
 * @brief 去激活非接触卡。
 *
 * @param user_data PC/SC 上下文。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_picc_deactivate(void *user_data)
{
    return emv_pcsc_icc_power_off(user_data);
}

/**
 * @brief 通过 PC/SC 发送非接触卡 APDU。
 *
 * @param user_data PC/SC 上下文。
 * @param command 输入 APDU。
 * @param command_len APDU 长度。
 * @param response 输出响应缓冲区。
 * @param response_len 输入时为缓冲区大小，输出时为响应长度。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
static int emv_pcsc_picc_transceive_apdu(void *user_data,
                                         const unsigned char *command,
                                         size_t command_len,
                                         unsigned char *response,
                                         size_t *response_len)
{
    return emv_pcsc_icc_transceive_apdu(user_data, command, command_len, response, response_len);
}

/**
 * @brief 获取最近一次底层 PC/SC 错误码。
 *
 * @param user_data PC/SC 上下文。
 *
 * @return 最近一次底层错误码。
 */
static int emv_pcsc_get_last_hw_error(void *user_data)
{
    EMVPcscContext *ctx = (EMVPcscContext *)user_data;

    if (!ctx) {
        return EMV_ERR_INVALID_PARAM;
    }
    return (int)ctx->last_hw_error;
}

/**
 * @brief 使用 PC/SC 实现初始化读卡器接口。
 *
 * @param reader_if 待读写的读卡器接口对象。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_reader_use_pcsc_driver(EMVReaderInterface *reader_if)
{
    if (!reader_if) {
        return EMV_ERR_INVALID_PARAM;
    }

    memset(&g_pcsc_context, 0, sizeof(g_pcsc_context));
    memset(reader_if, 0, sizeof(*reader_if));

    reader_if->user_data = &g_pcsc_context;
    reader_if->open = emv_pcsc_open;
    reader_if->close = emv_pcsc_close;
    reader_if->get_status = emv_pcsc_get_status;
    reader_if->poll_card = emv_pcsc_poll_card;
    reader_if->cancel_io = emv_pcsc_cancel_io;
    reader_if->icc_power_on = emv_pcsc_icc_power_on;
    reader_if->icc_power_off = emv_pcsc_icc_power_off;
    reader_if->icc_transceive_apdu = emv_pcsc_icc_transceive_apdu;
    reader_if->picc_activate = emv_pcsc_picc_activate;
    reader_if->picc_deactivate = emv_pcsc_picc_deactivate;
    reader_if->picc_transceive_apdu = emv_pcsc_picc_transceive_apdu;
    reader_if->get_last_hw_error = emv_pcsc_get_last_hw_error;
    return EMV_OK;
}
