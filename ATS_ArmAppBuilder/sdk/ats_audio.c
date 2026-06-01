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
#include "ats_error.h"

#define ATS_AUDIO_RPC_TIMEOUT_MS  5000U

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value & 0xFFU);
    buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
    buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0]
         | ((uint32_t)buffer[1] << 8)
         | ((uint32_t)buffer[2] << 16)
         | ((uint32_t)buffer[3] << 24);
}

int ats_audio_set_volume(size_t uiVolume)
{
    uint8_t req[4];
    write_u32_le(req, (uint32_t)uiVolume);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_SET_VOLUME,
                              req, sizeof(req), resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_audio_get_volume(size_t *puiVolume)
{
    if (!puiVolume)
        return -1;

    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_GET_VOLUME,
                              NULL, 0, resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    int32_t result = (int32_t)read_u32_le(resp);
    if (result != 0)
        return result;

    if (resp_len < 8)
        return -1;

    *puiVolume = (size_t)read_u32_le(&resp[4]);
    return 0;
}

int ats_audio_play_file(const char *pcFilePath)
{
    if (!pcFilePath)
        return -1;

    uint16_t path_len = (uint16_t)strlen(pcFilePath);
    if (path_len > 255U)
        return -1;

    uint8_t req[256];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pcFilePath, path_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_PLAY_FILE,
                              req, (uint16_t)(1 + path_len),
                              resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

int ats_audio_init(void)
{
    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_INIT,
                              NULL, 0, resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 4)
        return -1;

    return (int)read_u32_le(resp);
}

void ats_audio_shutdown(void)
{
    ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_SHUTDOWN,
                    NULL, 0, NULL, NULL, ATS_AUDIO_RPC_TIMEOUT_MS);
}

bool ats_audio_is_playing(void)
{
    uint8_t resp[1];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_AUDIO, ATS_RPC_AUDIO_CMD_IS_PLAYING,
                              NULL, 0, resp, &resp_len, ATS_AUDIO_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK || resp_len < 1)
        return false;

    return resp[0] != 0U;
}
