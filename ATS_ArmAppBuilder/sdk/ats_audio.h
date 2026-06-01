#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

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

/**
 * @brief 初始化音频播放子系统（启动后台播放线程）
 * 
 * 通常不需要手动调用，ats_audio_play_file 会自动初始化。
 * 也可在程序启动时提前调用以避免首次播放的延迟。
 * 
 * @return 0:成功 <0:失败
 */
int ats_audio_init(void);

/**
 * @brief 关闭音频播放子系统
 *
 * 停止后台播放线程，等待当前正在播放的音频播完后退出。
 * 应在程序退出时调用。
 */
void ats_audio_shutdown(void);

/**
 * @brief 查询是否有音频正在播放
 *
 * @return true: 正在播放 false: 未播放
 */
bool ats_audio_is_playing(void);

#ifdef __cplusplus
}
#endif
