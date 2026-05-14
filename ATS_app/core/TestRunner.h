#pragma once

#include <QObject>
#include <QThread>
#include <QVector>
#include <QStringList>
#include <atomic>
#include "TestCase.h"

/**
 * @brief 测试执行器
 *
 * 负责：
 *  - 管理所有已注册的测试用例
 *  - 在独立线程中顺序执行测试
 *  - 发出进度/结果信号给 UI
 */
class TestRunner : public QObject
{
    Q_OBJECT

public:
    explicit TestRunner(QObject *parent = nullptr);
    ~TestRunner() override;

    /**
     * @brief 注册一个测试用例（TestRunner 接管所有权）
     */
    void registerCase(TestCase *tc);

    /**
     * @brief 获取所有已注册用例的只读列表
     */
    const QVector<TestCase *> &allCases() const { return m_cases; }

    /**
     * @brief 运行指定名称的用例列表
     */
    void runCases(const QStringList &names);

    /**
     * @brief 运行指定脚本路径列表（动态创建 LuaTestCase）
     * @param scriptMap 键值对：(显示名称, 脚本路径)
     */
    void runScripts(const QMap<QString, QString> &scriptMap);

    /**
     * @brief 运行所有已注册用例
     */
    void runAll();

    /**
     * @brief 请求停止当前运行
     */
    void stop();

    bool isRunning() const { return m_running; }

signals:
    void testStarted(const QString &caseName);
    void testFinished(const QString &caseName, bool passed, const QString &detail);
    void allTestsFinished(int total, int passed, int failed);
    void progressChanged(int current, int total);

private:
    void executeList(const QVector<TestCase *> &list);

    QVector<TestCase *>  m_cases;
    QThread             *m_workerThread = nullptr;
    std::atomic<bool>    m_running      = false;
    std::atomic<bool>    m_stopRequest  = false;
};

/* ─── 内部工作对象（在 workerThread 中运行）────────────────────────────────── */
class TestWorker : public QObject
{
    Q_OBJECT
public:
    explicit TestWorker(QObject *parent = nullptr) : QObject(parent) {}

    std::atomic<bool> *stopFlag = nullptr;

public slots:
    void execute(const QVector<TestCase *> &list);

signals:
    void testStarted(const QString &name);
    void testFinished(const QString &name, bool passed, const QString &detail);
    void allDone(int total, int passed, int failed);
    void progressChanged(int current, int total);
};
