/**
 * @file ats_audio.c
 * @brief ATS音频功能 - RPC代理实现（Target端）
 *
 * 所有音频操作通过 RPC 请求转发到 Host 端执行。
 * Host 端收到消息后调用其本地的 ats_audio.c 实现。
 */

#include "ats_audio.h"

#include <string.h>
#include <stdlib.h>

#include "ats_rpc.h"
#include "ats_sys.h"
#include "ats_error.h"

#define ATS_AUDIO_RPC_TIMEOUT_MS  500U

int ats_audio_set_volume(size_t uiVolume)
{
    uint8_t req[4];
    ats_rpc_write_u32_le(req, (uint32_t)uiVolume);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_SET_VOLUME,
                              req, sizeof(req), resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;

    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_audio_get_volume(size_t *puiVolume)
{
    if (!puiVolume)
        return ATS_EC_INVALID_PARAM;

    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_GET_VOLUME,
                              NULL, 0, resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;

    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    int32_t result = (int32_t)ats_rpc_read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < 8)
        return ATS_EC_BAD_DATA;

    *puiVolume = (size_t)ats_rpc_read_u32_le(&resp[4]);
    return ATS_EC_OK;
}

int ats_audio_play_file(const char *pcFilePath)
{
    if (!pcFilePath)
        return ATS_EC_INVALID_PARAM;

    uint16_t path_len = (uint16_t)strlen(pcFilePath);
    if (path_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint8_t req[256];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pcFilePath, path_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_PLAY_FILE,
                              req, (uint16_t)(1 + path_len),
                              resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;

    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}
