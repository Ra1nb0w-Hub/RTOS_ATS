#pragma once

#include <QObject>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <functional>

/**
 * @brief 日志管理器（单例）
 *
 * 职责：
 *  1. 接收来自 C 代码（sysapi）的日志（通过 C 回调桥接）
 *  2. 接收来自 Qt C++ 代码的日志
 *  3. 通过 Qt 信号/回调把日志推送给 UI
 *  4. 可选：同时写入文件
 */
class LogManager : public QObject
{
    Q_OBJECT

public:
    static LogManager *instance();

    /**
     * @brief 注册 UI 输出回调（由 MainWindow 设置）
     * @param cb  参数：(message, level)
     */
    using OutputCallback = std::function<void(const QString &msg, const QString &level)>;
    void setOutputCallback(OutputCallback cb);

    /**
     * @brief 写入一条日志（线程安全）
     */
    void write(const QString &level, const QString &msg);

    /**
     * @brief 开启日志文件保存
     * @param path 文件路径
     */
    bool openLogFile(const QString &path);
    void closeLogFile();

    /**
     * @brief 静态便利函数（供 C++ 层使用）
     */
    static void log(const QString &level, const QString &msg);
    static void logSys(const QString &msg);
    static void logInfo(const QString &msg);
    static void logWarn(const QString &msg);
    static void logError(const QString &msg);
    static void logDebug(const QString &msg);

signals:
    /**
     * @brief 新日志信号（跨线程安全，Qt::QueuedConnection）
     */
    void newLog(const QString &msg, const QString &level);

private:
    explicit LogManager(QObject *parent = nullptr);
    ~LogManager() override;

    static LogManager *s_instance;

    OutputCallback  m_callback;
    QMutex          m_mutex;
    QFile          *m_logFile    = nullptr;
    QTextStream    *m_logStream  = nullptr;
};
