#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief 设置音量
 * 
 * @param[in] uiVolume 音量值, 0-100
 * 
 * @return 0:成功 <0:失败
 */
int ats_audio_set_volume(size_t uiVolume);

/**
 * @brief 获取音量
 * 
 * @param[out] puiVolume 音量值, 0-100
 * 
 * @return 0:成功 <0:失败
 */
int ats_audio_get_volume(size_t *puiVolume);

/**
 * @brief 播放音频文件(仅支持MP3格式)
 * 
 * 异步模式：将文件追加到播放队列，立即返回。
 * 后台线程会依次播放队列中的每个音频。
 * 首次调用时自动初始化播放线程。
 * 
 * @param[in] pcFilePath 音频文件路径（相对路径，基于 ats_fs 根目录）
 * 
 * @return 0:成功(已入队) <0:失败
 */
int ats_audio_play_file(const char *pcFilePath);

#ifdef __cplusplus
}
#endif
