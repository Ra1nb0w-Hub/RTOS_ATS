#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 文件句柄类型
typedef int ats_fs_handle_t;

// 打开文件的标志
typedef enum {
    ATS_FS_FLAG_OPEN = 0,           // 打开文件
    ATS_FS_FLAG_CREATE,             // 始终创建并打开文件
} ats_fs_open_flags_t;

// 打开文件的模式
typedef enum {
    ATS_FS_MODE_RDONLY = 0,         // 只读
    ATS_FS_MODE_WRONLY,             // 只写
    ATS_FS_MODE_RDWR,               // 读写
} ats_fs_open_mode_t;

// 文件偏移位置
typedef enum {
    ATS_FS_WHENCE_BEGIN = 0,        // 文件开头
    ATS_FS_WHENCE_CURRENT,          // 当前位置
    ATS_FS_WHENCE_END,              // 文件末尾
} ats_fs_whence_t;

/**
 * @brief 打开文件
 * 
 * @param handle 文件句柄
 * @param pathName 文件路径
 * @param flags 打开标志
 * @param mode 打开模式
 * 
 * @return 0:成功 <0:失败
 */
int ats_fs_open(ats_fs_handle_t *handle, const char *pathName, ats_fs_open_flags_t flags, ats_fs_open_mode_t mode);

/**
 * @brief 关闭文件
 * 
 * @param handle 文件句柄
 * 
 * @return 0:成功 <0:失败
 */
int ats_fs_close(ats_fs_handle_t handle);

/**
 * @brief 从文件读取数据
 * 
 * @param handle 文件句柄
 * @param buf 缓冲区
 * @param count 要读取的字节数
 * 
 * @return 实际读取的字节数
 */
int ats_fs_read(ats_fs_handle_t handle, void *buf, size_t count);

/**
 * @brief 向文件写入数据
 * 
 * @param handle 文件句柄
 * @param buf 缓冲区
 * @param count 要写入的字节数
 * 
 * @return 实际写入的字节数
 */
int ats_fs_write(ats_fs_handle_t handle, const void *buf, size_t count);

/**
 * @brief 设置文件偏移位置
 * 
 * @param handle 文件句柄
 * @param offset 偏移量
 * @param whence 偏移位置
 * 
 * @return 0:成功 <0:失败
 */
int ats_fs_seek(ats_fs_handle_t handle, size_t offset, ats_fs_whence_t whence);

/**
 * @brief 获取文件大小
 * 
 * @param pathName 文件路径
 * 
 * @return 文件大小
 */
int ats_fs_size(const char *pathName);

/**
 * @brief 删除文件
 * 
 * @param pathName 文件路径
 * 
 * @return 0:成功 <0:失败
 */
int ats_fs_remove(const char *pathName);

/**
 * @brief 文件是否存在
 * 
 * @param pathName 文件路径
 * 
 * @return 0:存在 -1:不存在
 */
int ats_fs_exist(const char *pathName);

#ifdef __cplusplus
}
#endif