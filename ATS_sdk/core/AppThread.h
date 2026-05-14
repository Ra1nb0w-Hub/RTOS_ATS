#pragma once

#include <QThread>
#include <QString>
#include <atomic>

/**
 * @brief AppThread — 在独立 QThread 中运行嵌入式 app 主入口 ats_main()
 *
 * 程序启动后自动调用 startApp()，ats_main() 在后台线程中运行直到程序退出。
 * 不再支持手动 stop/reboot，避免了 terminate() 导致的资源泄漏问题。
 */
class AppThread : public QThread
{
    Q_OBJECT

public:
    explicit AppThread(QObject *parent = nullptr);
    ~AppThread() override;

    /**
     * @brief 启动 app（在新线程中调用 ats_main）
     */
    void startApp();

    /**
     * @brief app 是否正在运行
     */
    bool isAppRunning() const;

protected:
    void run() override;

private:
    std::atomic<bool> m_appRunning{false};
};
