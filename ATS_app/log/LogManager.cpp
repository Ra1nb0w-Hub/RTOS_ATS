#include "LogManager.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QThread>

#include "sdk/ats_sys.h"

LogManager *LogManager::s_instance = nullptr;

/* ─── C 桥接回调（被 ats_sys.c 文件调用）──────────────────────────────────── */
static void c_log_callback(const char *level, const char *msg)
{
    LogManager::log(QString::fromUtf8(level), QString::fromUtf8(msg));
}

LogManager::LogManager(QObject *parent)
    : QObject(parent)
{
    /* 将 C 层日志桥接到本管理器 */
    ats_log_set_callback(c_log_callback);
}

LogManager::~LogManager()
{
    ats_log_set_callback(nullptr);
    closeLogFile();
}

LogManager *LogManager::instance()
{
    if (!s_instance)
        s_instance = new LogManager();
    return s_instance;
}

void LogManager::setOutputCallback(OutputCallback cb)
{
    QMutexLocker lock(&m_mutex);
    m_callback = cb;
}

void LogManager::write(const QString &level, const QString &msg)
{
    QMutexLocker lock(&m_mutex);

    /* 写文件 */
    if (m_logStream) {
        *m_logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
                     << " [" << level << "] " << msg << "\n";
        m_logStream->flush();
    }

    /* UI 回调（需在主线程执行，通过信号队列）*/
    emit newLog(msg, level);

    if (m_callback) {
        /* 如果当前是主线程，直接调用；否则通过 QMetaObject 排队 */
        if (QThread::currentThread() == this->thread()) {
            m_callback(msg, level);
        } else {
            QString msgCopy  = msg;
            QString levelCopy = level;
            QMetaObject::invokeMethod(this, [this, msgCopy, levelCopy]() {
                if (m_callback) m_callback(msgCopy, levelCopy);
            }, Qt::QueuedConnection);
        }
    }
}

bool LogManager::openLogFile(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    closeLogFile();
    m_logFile = new QFile(path);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete m_logFile;
        m_logFile = nullptr;
        return false;
    }
    m_logStream = new QTextStream(m_logFile);
    return true;
}

void LogManager::closeLogFile()
{
    delete m_logStream;  m_logStream = nullptr;
    if (m_logFile) { m_logFile->close(); delete m_logFile; m_logFile = nullptr; }
}

/* ─── 静态便利函数 ──────────────────────────────────────────────────────────── */
void LogManager::log(const QString &level, const QString &msg)   { instance()->write(level, msg); }
void LogManager::logSys(const QString &msg)                       { instance()->write("SYS",   msg); }
void LogManager::logInfo(const QString &msg)                      { instance()->write("INFO",  msg); }
void LogManager::logWarn(const QString &msg)                      { instance()->write("WARN",  msg); }
void LogManager::logError(const QString &msg)                     { instance()->write("ERROR", msg); }
void LogManager::logDebug(const QString &msg)                     { instance()->write("DEBUG", msg); }
