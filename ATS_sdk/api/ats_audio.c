/**
 * @file ats_audio.c
 * @brief ATS音频功能 - Windows平台实现
 *
 * 音量控制：使用 Windows Core Audio API（IAudioEndpointVolume）
 * MP3播放：使用 Windows MCI（mciSendString），异步队列模式
 *   - 调用 ats_audio_play_file() 将音频路径追加到队列，立即返回
 *   - 后台播放线程逐个取出队列中的音频，异步播放+轮询检测结束
 *   - 关闭时可立即停止，响应时间 < 100ms
 *
 * 编译依赖：
 *   - ole32.lib
 *   - winmm.lib
 */

#include "ats_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ats_fs.h"

/* ---- Windows Core Audio API ---- */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define COBJMACROS
#include <windows.h>
#include <initguid.h>          /* 使 mmdeviceapi.h / endpointvolume.h 中的 DEFINE_GUID 生成定义而非 extern 声明 */
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <mmsystem.h>

/* =========================================================
 * 内部辅助：获取默认音频终端音量接口
 * ========================================================= */

static IAudioEndpointVolume *get_audio_endpoint_volume(void)
{
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioEndpointVolume *pVolume = NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return NULL;

    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL,
        &IID_IMMDeviceEnumerator,
        (void **)&pEnumerator);
    if (FAILED(hr))
        return NULL;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
        pEnumerator, eRender, eConsole, &pDevice);
    IMMDeviceEnumerator_Release(pEnumerator);
    if (FAILED(hr))
        return NULL;

    hr = IMMDevice_Activate(
        pDevice,
        &IID_IAudioEndpointVolume,
        CLSCTX_ALL,
        NULL,
        (void **)&pVolume);
    IMMDevice_Release(pDevice);
    if (FAILED(hr))
        return NULL;

    return pVolume;
}

/* =========================================================
 * 音量接口实现
 * ========================================================= */

int ats_audio_set_volume(size_t uiVolume)
{
    if (uiVolume > 100)
        uiVolume = 100;

    IAudioEndpointVolume *pVolume = get_audio_endpoint_volume();
    if (!pVolume)
        return -1;

    /* 将 0-100 的整数音量转换为 0.0-1.0 的浮点数 */
    float fVolume = (float)uiVolume / 100.0f;
    HRESULT hr = IAudioEndpointVolume_SetMasterVolumeLevelScalar(pVolume, fVolume, NULL);

    IAudioEndpointVolume_Release(pVolume);
    return SUCCEEDED(hr) ? 0 : -1;
}

int ats_audio_get_volume(size_t *puiVolume)
{
    if (!puiVolume)
        return -1;

    IAudioEndpointVolume *pVolume = get_audio_endpoint_volume();
    if (!pVolume)
        return -1;

    float fVolume = 0.0f;
    HRESULT hr = IAudioEndpointVolume_GetMasterVolumeLevelScalar(pVolume, &fVolume);
    IAudioEndpointVolume_Release(pVolume);

    if (FAILED(hr))
        return -1;

    /* 将 0.0-1.0 转换为 0-100 */
    *puiVolume = (size_t)(fVolume * 100.0f + 0.5f);
    if (*puiVolume > 100)
        *puiVolume = 100;
    return 0;
}

/* =========================================================
 * 异步音频播放队列
 *
 * - 单链表队列，头出尾入
 * - 一个后台线程循环消费队列，同步播放每个音频
 * - ats_audio_play_file() 只负责入队，立即返回
 * ========================================================= */

#define ATS_AUDIO_QUEUE_MAX 64

typedef struct AudioNode {
    char *path;                /* malloc 分配的完整路径，播放后释放 */
    struct AudioNode *next;
} AudioNode;

static AudioNode *s_queue_head = NULL;   /* 队列头（出队端） */
static AudioNode *s_queue_tail = NULL;   /* 队列尾（入队端） */
static int        s_queue_count = 0;     /* 当前队列长度 */
static HANDLE     s_queue_sem  = NULL;   /* 信号量：有新音频可播放 */
static HANDLE     s_audio_thread = NULL; /* 播放线程句柄 */
static volatile int s_audio_running = 0; /* 线程运行标志 */
static volatile int s_audio_playing = 0; /* 正在同步播放标志（用于 shutdown 时打断） */

CRITICAL_SECTION g_audio_cs;

/* ---- 内部：播放单个文件（异步播放 + 轮询退出标志） ---- */
static void play_one(const char *fullPath)
{
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias ats_mp3", fullPath);
    MCIERROR err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err != 0)
        return;

    s_audio_playing = 1;

    /* 异步播放（不带 wait），通过轮询检测播放状态 */
    mciSendStringA("play ats_mp3", NULL, 0, NULL);

    /* 轮询：等待播放结束或收到退出信号 */
    char buf[64];
    while (s_audio_running)
    {
        Sleep(50); /* 50ms 轮询间隔，关闭响应 < 100ms */

        buf[0] = '\0';
        mciSendStringA("status ats_mp3 mode", buf, sizeof(buf), NULL);
        /* "stopped" 表示播放自然结束 */
        if (strncmp(buf, "stopped", 7) == 0)
            break;
    }

    s_audio_playing = 0;
    mciSendStringA("close ats_mp3", NULL, 0, NULL);
}

/* ---- 入队（线程安全） ---- */
static int queue_push(const char *path)
{
    if (s_queue_count >= ATS_AUDIO_QUEUE_MAX)
        return -1;

    AudioNode *node = (AudioNode *)malloc(sizeof(AudioNode));
    if (!node)
        return -1;

    node->path = _strdup(path);
    node->next = NULL;

    EnterCriticalSection(&g_audio_cs);

    if (s_queue_tail)
        s_queue_tail->next = node;
    else
        s_queue_head = node;
    s_queue_tail = node;
    s_queue_count++;

    LeaveCriticalSection(&g_audio_cs);

    ReleaseSemaphore(s_queue_sem, 1, NULL);
    return 0;
}

/* ---- 出队（线程安全） ---- */
static char *queue_pop(void)
{
    /* 先等待信号量 */
    WaitForSingleObject(s_queue_sem, INFINITE);

    EnterCriticalSection(&g_audio_cs);

    if (!s_queue_head)
    {
        LeaveCriticalSection(&g_audio_cs);
        return NULL;
    }

    AudioNode *node = s_queue_head;
    char *path = node->path;
    s_queue_head = node->next;
    if (!s_queue_head)
        s_queue_tail = NULL;
    s_queue_count--;

    LeaveCriticalSection(&g_audio_cs);

    free(node);
    return path;
}

/* ---- 播放线程 ---- */
static DWORD WINAPI audio_thread_func(LPVOID param)
{
    (void)param;
    while (s_audio_running)
    {
        char *path = queue_pop();
        if (!path)
            continue;

        /* 再次检查运行标志（可能被 shutdown 唤醒） */
        if (!s_audio_running)
        {
            free(path);
            break;
        }

        play_one(path);
        free(path);
    }
    /* 退出时不再播完队列，剩余音频由 shutdown 直接释放 */
    return 0;
}

/* ---- 初始化音频播放子系统 ---- */
int ats_audio_init(void)
{
    if (s_audio_running)
        return 0;

    InitializeCriticalSection(&g_audio_cs);
    s_queue_sem = CreateSemaphore(NULL, 0, ATS_AUDIO_QUEUE_MAX, NULL);
    if (!s_queue_sem)
        return -1;

    s_audio_running = 1;
    s_audio_thread = CreateThread(NULL, 0, audio_thread_func, NULL, 0, NULL);
    if (!s_audio_thread)
    {
        s_audio_running = 0;
        CloseHandle(s_queue_sem);
        s_queue_sem = NULL;
        DeleteCriticalSection(&g_audio_cs);
        return -1;
    }

    return 0;
}

/* ---- 关闭音频播放子系统（立即停止） ---- */
void ats_audio_shutdown(void)
{
    if (!s_audio_running)
        return;

    /* 通知播放线程立即退出 */
    s_audio_running = 0;

    /* 如果线程正在 play_one 的 Sleep 轮询中，不需要额外唤醒 */
    /* 如果线程阻塞在信号量等待中，释放一个使其检测退出标志 */
    ReleaseSemaphore(s_queue_sem, 1, NULL);

    if (s_audio_thread)
    {
        /* 线程最多 50ms 就能检测到退出标志 */
        WaitForSingleObject(s_audio_thread, 1000);
        CloseHandle(s_audio_thread);
        s_audio_thread = NULL;
    }

    if (s_queue_sem)
    {
        CloseHandle(s_queue_sem);
        s_queue_sem = NULL;
    }

    /* 清空队列残留（不播放，直接释放） */
    EnterCriticalSection(&g_audio_cs);
    while (s_queue_head)
    {
        AudioNode *node = s_queue_head;
        s_queue_head = node->next;
        free(node->path);
        free(node);
    }
    s_queue_tail = NULL;
    s_queue_count = 0;
    LeaveCriticalSection(&g_audio_cs);

    DeleteCriticalSection(&g_audio_cs);
}

/* =========================================================
 * 对外接口：异步播放音频文件
 *
 * 将文件路径追加到播放队列，立即返回。
 * 后台线程会依次播放队列中的每个音频。
 * ========================================================= */

int ats_audio_play_file(const char *pcFilePath)
{
    if (!pcFilePath)
        return -1;

    /* 确保播放线程已启动 */
    if (!s_audio_running)
    {
        if (ats_audio_init() != 0)
            return -1;
    }

    /* 将相对路径解析为完整路径（基于 ats_fs 根目录） */
    char *fullPath = ats_fs_fullpath(pcFilePath);
    if (!fullPath)
        return -1;

    /* 追加到队列尾部 */
    int ret = queue_push(fullPath);
    free(fullPath);
    return ret;
}

int ats_audio_get_queue_count(void)
{
    /* 惰性初始化，确保 g_audio_cs 已初始化 */
    if (!s_audio_running)
    {
        if (ats_audio_init() != 0)
            return -1;
    }

    EnterCriticalSection(&g_audio_cs);
    int count = s_queue_count;
    LeaveCriticalSection(&g_audio_cs);
    return count;
}

bool ats_audio_is_playing(void)
{
    /* 惰性初始化，确保 g_audio_cs 已初始化 */
    if (!s_audio_running)
    {
        if (ats_audio_init() != 0)
            return false;
    }

    EnterCriticalSection(&g_audio_cs);
    bool playing = s_audio_playing != 0;
    LeaveCriticalSection(&g_audio_cs);
    return playing;
}
