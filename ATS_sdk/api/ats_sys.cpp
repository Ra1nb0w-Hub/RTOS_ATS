/**
 * @file ats_sys.cpp
 * @brief ATS系统功能 - Qt跨平台实现
 *
 * 实现内容：
 *   - 内存分配/释放
 *   - 日志打印
 *   - 按键事件
 *   - 线程创建/休眠
 *   - 互斥锁
 *   - 信号量
 *   - 日期时间/时间戳/Tick
 *   - 随机数
 *
 * 所有 Windows API 已替换为 Qt 等价实现：
 *   CRITICAL_SECTION  -> QMutex
 *   CreateThread      -> QThread
 *   Sleep             -> QThread::msleep
 *   CreateMutex       -> QMutex
 *   CreateSemaphore   -> QSemaphore
 *   GetTickCount      -> QElapsedTimer::elapsed
 *   CryptGenRandom    -> QRandomGenerator
 *
 * 对外接口（ats_sys.h）保持 void* 句柄，内部通过 new 分配 Qt 对象，
 * OPayThread.c 等调用方无需任何修改。
 */

#include "ats_sys.h"
#include "ats_error.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>

#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QRandomGenerator>

/* =========================================================
 * 内部状态
 * ========================================================= */

/* 日志回调（由 Qt 层通过 ats_log_set_callback 注册） */
static ats_log_callback_t s_log_callback = NULL;

/* 按键事件队列（简单单元素队列） */
static ats_keypad_event_t s_keypad_event = {ATS_KEY_CODE_NONE, false};
static QMutex s_keypad_mutex;
static char s_serial_number[32] = {0};

/* 全局 elapsed timer（用于 tick 计时） */
static QElapsedTimer s_tick_timer;

/* 确保 tick timer 在首次调用时启动 */
static void ensure_tick_timer()
{
    if (!s_tick_timer.isValid())
        s_tick_timer.start();
}

/* =========================================================
 * 内存
 * ========================================================= */

void *ats_malloc(unsigned int size)
{
    return malloc(size);
}

void ats_free(void *ptr)
{
    if (ptr)
    {
        free(ptr);
    }
}

/* =========================================================
 * 日志
 * ========================================================= */

void ats_log_set_callback(ats_log_callback_t callback)
{
    s_log_callback = callback;
}

void ats_log_print(const char *level, const char *string)
{
    if (s_log_callback && level && string)
    {
        s_log_callback(level, string);
    }
}

void ats_log_printf(const char *level, const char *format, ...)
{
    if (!s_log_callback || !level || !format)
        return;

    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    s_log_callback(level, buf);
}

/* =========================================================
 * 按键事件
 * ========================================================= */

int ats_keypad_set_event(uint8_t keyCode, bool status)
{
    s_keypad_mutex.lock();
    s_keypad_event.key_code = (ats_key_code_t)keyCode;
    s_keypad_event.press_status = status;
    s_keypad_mutex.unlock();
    return 0;
}

int ats_keypad_get_event(ats_keypad_event_t *event)
{
    if (!event)
        return -1;
    s_keypad_mutex.lock();
    *event = s_keypad_event;
    s_keypad_mutex.unlock();
    return 0;
}

/* =========================================================
 * 线程
 * ========================================================= */
class AtsThread;

/* 全局线程注册表 */
static QSet<AtsThread*>  g_atsThreads;
static QMutex            g_atsThreadsMutex;

/* 线程入口包装：将 C 函数指针适配为 QThread 子类 */
class AtsThread : public QThread
{
public:
    AtsThread(void (*func)(void *args), void *args, QThread::Priority priority)
        : m_func(func), m_args(args)
    {
        setObjectName("AtsThread");

        /* 注册到全局列表 */
        g_atsThreadsMutex.lock();
        g_atsThreads.insert(this);
        g_atsThreadsMutex.unlock();

        start(priority);
    }

    ~AtsThread() override
    {
        /* 从全局列表移除 — 不使用 QMutexLocker 避免与 kill_all 死锁 */
        g_atsThreadsMutex.lock();
        g_atsThreads.remove(this);
        g_atsThreadsMutex.unlock();
    }

protected:
    void run() override
    {
        if (m_func)
            m_func(m_args);
    }

private:
    void (*m_func)(void *args);
    void *m_args;
};

int ats_thread_create(ats_thread_handle_t *handle, const char *name,
                      ats_thread_priority_t priority,
                      unsigned int stackSize,
                      void (*func)(void *args), void *args)
{
    (void)stackSize; /* Qt 不支持直接设置线程栈大小 */

    if (!func)
        return -1;

    /* 映射优先级 */
    QThread::Priority qPriority = QThread::NormalPriority;
    switch (priority)
    {
    case ATS_THREAD_PRIORITY_LOWEST:
        qPriority = QThread::LowestPriority;
        break;
    case ATS_THREAD_PRIORITY_LOW:
        qPriority = QThread::LowPriority;
        break;
    case ATS_THREAD_PRIORITY_NORMAL:
        qPriority = QThread::NormalPriority;
        break;
    case ATS_THREAD_PRIORITY_HIGH:
        qPriority = QThread::HighPriority;
        break;
    case ATS_THREAD_PRIORITY_HIGHEST:
        qPriority = QThread::HighestPriority;
        break;
    default:
        qPriority = QThread::NormalPriority;
        break;
    }

    AtsThread *thread = new AtsThread(func, args, qPriority);
    if (name)
        thread->setObjectName(QString::fromUtf8(name));

    if (handle)
    {
        *handle = static_cast<ats_thread_handle_t>(thread);
    }
    else
    {
        /* 无需 handle 时，线程结束后自动释放 */
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    }

    return 0;
}

int ats_thread_sleep(unsigned int ms)
{
    QThread::msleep(ms);
    return 0;
}

/* =========================================================
 * 全局线程管理
 * ========================================================= */

int ats_thread_kill_all(void)
{
    /* 先复制线程列表，然后立即释放 mutex */
    g_atsThreadsMutex.lock();
    QList<QThread*> threads;
    for (auto *t : g_atsThreads)
        threads.append(t);
    g_atsThreads.clear();
    g_atsThreadsMutex.unlock();

    int count = 0;
    for (auto *thread : threads)
    {
        if (thread->isRunning())
        {
            thread->terminate();
            count++;
        }
    }

    /* 在 mutex 外等待所有线程结束，避免与 ~AtsThread 析构死锁 */
    for (auto *thread : threads)
    {
        if (thread->isRunning())
            thread->wait(1000);
    }

    /* 手动删除已结束的线程（断开 deleteLater 连接防止二次释放） */
    for (auto *thread : threads)
    {
        thread->disconnect(thread, &QThread::finished, thread, &QObject::deleteLater);
        delete thread;
    }

    return count;
}

/* =========================================================
 * 互斥锁
 *
 * 内部 new QMutex，通过 void* 句柄暴露给调用方。
 * QMutex 是可重入的（默认 Recursive=false，与 Windows Mutex 一致）。
 * ========================================================= */

int ats_mutex_create(ats_mutex_handle_t *handle, const char *name)
{
    (void)name;
    if (!handle)
        return -1;

    QMutex *mutex = new QMutex();
    *handle = static_cast<ats_mutex_handle_t>(mutex);
    return 0;
}

int ats_mutex_lock(ats_mutex_handle_t *handle)
{
    if (!handle || !*handle)
        return -1;
    QMutex *mutex = static_cast<QMutex *>(*handle);
    mutex->lock();
    return 0;
}

int ats_mutex_unlock(ats_mutex_handle_t *handle)
{
    if (!handle || !*handle)
        return -1;
    QMutex *mutex = static_cast<QMutex *>(*handle);
    mutex->unlock();
    return 0;
}

/* =========================================================
 * 信号量
 *
 * 内部 new QSemaphore，通过 void* 句柄暴露给调用方。
 * ========================================================= */

int ats_semaphore_create(ats_semaphore_handle_t *handle, const char *name,
                         unsigned int count)
{
    (void)name;
    if (!handle)
        return -1;

    QSemaphore *sem = new QSemaphore(count);
    *handle = static_cast<ats_semaphore_handle_t>(sem);
    return 0;
}

int ats_semaphore_wait(ats_semaphore_handle_t *handle, unsigned int timeout)
{
    if (!handle || !*handle)
        return -1;

    QSemaphore *sem = static_cast<QSemaphore *>(*handle);

    if (timeout == 0xFFFFFFFFU)
    {
        /* 永久等待：改为循环 tryAcquire，每次等 500ms，可以被 terminate() 打断 */
        while (!sem->tryAcquire(1, 500))
        {
            /* 循环等待，terminate() 可以在此期间终止线程 */
        }
        return 0;
    }

    /* 带超时的等待 */
    bool ok = sem->tryAcquire(1, timeout);
    return ok ? 0 : ATS_EC_TIMEOUT;
}

int ats_semaphore_post(ats_semaphore_handle_t *handle)
{
    if (!handle || !*handle)
        return -1;
    QSemaphore *sem = static_cast<QSemaphore *>(*handle);
    sem->release(1);
    return 0;
}

/* =========================================================
 * 日期时间 / 时间戳 / Tick
 * ========================================================= */

int ats_datetime_get(ats_datetime_t *datetime)
{
    if (!datetime)
        return -1;

    time_t now = time(nullptr);
    struct tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    datetime->uiYear   = (unsigned int)(utc.tm_year + 1900);
    datetime->uiMonth  = (unsigned int)(utc.tm_mon + 1);
    datetime->uiDay    = (unsigned int)(utc.tm_mday);
    datetime->uiHour   = (unsigned int)(utc.tm_hour);
    datetime->uiMinute = (unsigned int)(utc.tm_min);
    datetime->uiSecond = (unsigned int)(utc.tm_sec);

    return 0;
}

int ats_datetime_set(ats_datetime_t *datetime)
{
    (void)datetime;
    return 0;
}

unsigned long ats_timestamp_get(void)
{
    return (unsigned long)time(nullptr);
}

unsigned int ats_tick_get(void)
{
    ensure_tick_timer();
    return (unsigned int)s_tick_timer.elapsed();
}

/* =========================================================
 * 随机数
 *
 * 使用 QRandomGenerator::global()->fillRange() 生成密码学安全的随机字节。
 * ========================================================= */

int ats_random(unsigned int len, unsigned char *output)
{
    if (!output || len == 0)
        return -1;

    /* QRandomGenerator::fillRange 填充 quint32 数组，然后按字节拷贝 */
    quint32 buf[64];
    unsigned int offset = 0;

    while (offset < len)
    {
        /* 每次生成最多 64 * 4 = 256 字节 */
        unsigned int chunk = len - offset;
        unsigned int words = (chunk + 3) / 4;
        if (words > 64)
            words = 64;

        QRandomGenerator::global()->fillRange(buf, words);
        unsigned int bytes = words * 4;
        if (bytes > chunk)
            bytes = chunk;

        memcpy(output + offset, buf, bytes);
        offset += bytes;
    }

    return 0;
}

char *ats_serial_number_get(void)
{
    return s_serial_number;
}

int ats_serial_number_set(char *serial_number)
{
    if (!serial_number)
        return -1;
    strncpy(s_serial_number, serial_number, sizeof(s_serial_number) - 1);
    s_serial_number[sizeof(s_serial_number) - 1] = '\0';
    return 0;
}
