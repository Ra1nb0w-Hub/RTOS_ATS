/**
 * @file ats_fs.c
 * @brief ATS文件系统功能 - RPC代理实现（Target端）
 *
 * 所有文件操作通过 RPC 请求转发到 Host 端执行。
 * Host 端收到消息后调用其本地的 ats_fs.c 实现。
 */

#include "ats_fs.h"

#include <string.h>
#include <stdlib.h>

#include "ats_rpc.h"
#include "ats_error.h"
#include "ats_sys.h"

#define ATS_FS_RPC_TIMEOUT_MS  500U

int ats_fs_open(ats_fs_handle_t *handle, const char *pathName,
                ats_fs_open_flags_t flags, ats_fs_open_mode_t mode)
{
    if (!handle || !pathName)
        return ATS_EC_INVALID_PARAM;

    uint16_t path_len = (uint16_t)strlen(pathName);
    if (path_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint8_t req[258];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pathName, path_len);
    req[1 + path_len] = (uint8_t)flags;
    req[2 + path_len] = (uint8_t)mode;

    uint8_t resp[8];
    uint16_t resp_len = sizeof(resp);
    int rpc_ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_OPEN,
                                  req, (uint16_t)(3 + path_len),
                                  resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (rpc_ret != ATS_EC_OK)
        return rpc_ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    int32_t ret = (int32_t)ats_rpc_read_u32_le(resp);
    if (ret != 0)
        return (int)ret;

    if (resp_len < 8)
        return ATS_EC_BAD_DATA;

    *handle = (ats_fs_handle_t)(int32_t)ats_rpc_read_u32_le(&resp[4]);
    return 0;
}

int ats_fs_close(ats_fs_handle_t handle)
{
    uint8_t req[4];
    ats_rpc_write_u32_le(req, (uint32_t)handle);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_CLOSE,
                              req, sizeof(req), resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_fs_read(ats_fs_handle_t handle, void *buf, size_t count)
{
    if (!buf || count == 0U)
        return ATS_EC_INVALID_PARAM;

    if (count > 0xFFFFU - 4U)
        count = 0xFFFFU - 4U;

    uint8_t req[8];
    ats_rpc_write_u32_le(req, (uint32_t)handle);
    ats_rpc_write_u32_le(&req[4], (uint32_t)count);

    uint16_t resp_len = (uint16_t)(4 + count);
    uint8_t *resp = (uint8_t *)ats_malloc(resp_len);
    if (!resp)
        return ATS_EC_NO_MEMORY;

    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_READ,
                              req, sizeof(req), resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
    {
        ats_free(resp);
        return ret;
    }
    if (resp_len < 4)
    {
        ats_free(resp);
        return ATS_EC_BAD_DATA;
    }

    int32_t bytes_read = (int32_t)ats_rpc_read_u32_le(resp);
    if (bytes_read > 0 && (uint16_t)(4 + bytes_read) <= resp_len)
    {
        memcpy(buf, &resp[4], (size_t)bytes_read);
    }
    else if (bytes_read < 0)
    {
        ats_free(resp);
        return (int)bytes_read;
    }

    ats_free(resp);
    return (int)bytes_read;
}

int ats_fs_write(ats_fs_handle_t handle, const void *buf, size_t count)
{
    if (!buf || count == 0U)
        return ATS_EC_INVALID_PARAM;

    if (count > 0xFFFFU - 4U)
        count = 0xFFFFU - 4U;

    uint16_t req_len = (uint16_t)(4 + count);
    uint8_t *req = (uint8_t *)ats_malloc(req_len);
    if (!req)
        return ATS_EC_NO_MEMORY;

    ats_rpc_write_u32_le(req, (uint32_t)handle);
    memcpy(&req[4], buf, count);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_WRITE,
                              req, req_len, resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    ats_free(req);

    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_fs_seek(ats_fs_handle_t handle, size_t offset, ats_fs_whence_t whence)
{
    uint8_t req[9];
    ats_rpc_write_u32_le(req, (uint32_t)handle);
    ats_rpc_write_u32_le(&req[4], (uint32_t)offset);
    req[8] = (uint8_t)whence;

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_SEEK,
                              req, sizeof(req), resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_fs_size(const char *pathName)
{
    if (!pathName)
        return ATS_EC_INVALID_PARAM;

    uint16_t path_len = (uint16_t)strlen(pathName);
    if (path_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint8_t req[256];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pathName, path_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_SIZE,
                              req, (uint16_t)(1 + path_len),
                              resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_fs_remove(const char *pathName)
{
    if (!pathName)
        return ATS_EC_INVALID_PARAM;

    uint16_t path_len = (uint16_t)strlen(pathName);
    if (path_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint8_t req[256];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pathName, path_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_REMOVE,
                              req, (uint16_t)(1 + path_len),
                              resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}

int ats_fs_exist(const char *pathName)
{
    if (!pathName)
        return ATS_EC_INVALID_PARAM;

    uint16_t path_len = (uint16_t)strlen(pathName);
    if (path_len > 255U)
        return ATS_EC_TOO_LARGE;

    uint8_t req[256];
    req[0] = (uint8_t)path_len;
    memcpy(&req[1], pathName, path_len);

    uint8_t resp[4];
    uint16_t resp_len = sizeof(resp);
    int ret = ats_rpc_request(ATS_RPC_SERVICE_FS, ATS_RPC_FS_CMD_EXIST,
                              req, (uint16_t)(1 + path_len),
                              resp, &resp_len, ATS_FS_RPC_TIMEOUT_MS);
    if (ret != ATS_EC_OK)
        return ret;
    if (resp_len < 4)
        return ATS_EC_BAD_DATA;

    return (int)ats_rpc_read_u32_le(resp);
}
