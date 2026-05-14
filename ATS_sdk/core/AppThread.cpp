#include "AppThread.h"
#include "../log/LogManager.h"

/* 声明原始嵌入式程序入口（来自 main.c）*/
#ifdef __cplusplus
extern "C" {
#endif
    extern void ats_main(void);
#ifdef __cplusplus
}
#endif

AppThread::AppThread(QObject *parent)
    : QThread(parent)
{
}

AppThread::~AppThread()
{
    /* 析构时等待线程结束（程序退出时自动清理） */
    if (isRunning()) {
        terminate();
        wait(3000);
    }
}

bool AppThread::isAppRunning() const
{
    return m_appRunning.load();
}

void AppThread::startApp()
{
    if (m_appRunning.load())
        return;

    m_appRunning.store(true);
    start();
}

void AppThread::run()
{
    m_appRunning.store(true);
    LogManager::logSys("AppThread: ats_main starting...");

    /* 运行嵌入式 app 入口（阻塞，内部是 while(1) 无限循环）*/
    ats_main();

    /* 正常不会到达这里，只有 terminate() 强制终止或程序退出时才会退出 run() */
    m_appRunning.store(false);
    LogManager::logSys("AppThread: ats_main exited.");
}
