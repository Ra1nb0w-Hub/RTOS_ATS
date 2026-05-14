#include "TestRunner.h"
#include "../log/LogManager.h"
#include "../lua/LuaTestCase.h"

#include <QMetaObject>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

/* ─── TestWorker ──────────────────────────────────────────────────────────── */
void TestWorker::execute(const QVector<TestCase *> &list)
{
    int total  = list.size();
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < total; ++i) {
        if (stopFlag && *stopFlag) break;

        TestCase *tc = list[i];
        emit testStarted(tc->name());
        emit progressChanged(i, total);

        LogManager::logSys(QString("--- 正在执行: [%1] ---").arg(tc->displayName()));

        bool ok = false;
        QString failReason("OK");
        try {
            ok = tc->run();
        } catch (const std::exception &e) {
            ok = false;
            LogManager::logSys(
                QString("捕获异常 [%1]: %2").arg(tc->displayName(), e.what()));
        } catch (...) {
            ok = false;
            LogManager::logSys(
                QString("未知的异常 [%1]").arg(tc->displayName()));
        }

        if (ok)
        {
            ++passed;
            LogManager::logSys(QString("--- 执行结束: [%1] PASS ---").arg(tc->displayName()));
        }
        else
        {
            failReason = tc->failReason();
            ++failed;
            LogManager::logSys(QString("--- 执行结束: [%1] FAILED(%2) ---").arg(tc->displayName(), failReason));
        }

        emit testFinished(tc->name(), ok, failReason);
    }

    emit progressChanged(total, total);
    emit allDone(total, passed, failed);

    QString result = failed == 0
        ? "全部通过"
        : QString("通过: %1个, 失败: %2个").arg(passed).arg(failed);
    LogManager::logSys(QString("=== 测试用例执行结束. 总计: %1个, %2 ===")
                           .arg(total).arg(result));
}

/* ─── TestRunner ──────────────────────────────────────────────────────────── */
TestRunner::TestRunner(QObject *parent)
    : QObject(parent)
{
    // 测试用例不再自动扫描，只能从 XML 配置文件导入
}

TestRunner::~TestRunner()
{
    stop();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
        delete m_workerThread;
    }
    qDeleteAll(m_cases);
}

void TestRunner::registerCase(TestCase *tc)
{
    if (tc) m_cases.append(tc);
}

void TestRunner::runCases(const QStringList &names)
{
    QVector<TestCase *> selected;
    for (auto *tc : m_cases) {
        if (names.contains(tc->name()))
            selected.append(tc);
    }
    if (selected.isEmpty()) return;
    executeList(selected);
}

void TestRunner::runScripts(const QMap<QString, QString> &scriptMap)
{
    QVector<TestCase *> selected;
    QMapIterator<QString, QString> it(scriptMap);
    while (it.hasNext()) {
        it.next();
        selected.append(new LuaTestCase(it.value(), it.key()));
    }
    if (selected.isEmpty()) return;
    
    // 注意：动态创建的 LuaTestCase 会在执行后自动删除
    executeList(selected);
}

void TestRunner::runAll()
{
    executeList(m_cases);
}

void TestRunner::stop()
{
    m_stopRequest = true;
}

void TestRunner::executeList(const QVector<TestCase *> &list)
{
    if (m_running) return;

    m_running     = true;
    m_stopRequest = false;

    // 清理上一次线程
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_workerThread = new QThread(this);
    TestWorker *worker = new TestWorker();
    worker->stopFlag   = &m_stopRequest;
    worker->moveToThread(m_workerThread);

    // 连接信号
    connect(worker, &TestWorker::testStarted,
            this,   &TestRunner::testStarted);
    connect(worker, &TestWorker::testFinished,
            this,   &TestRunner::testFinished);
    connect(worker, &TestWorker::progressChanged,
            this,   &TestRunner::progressChanged);
    connect(worker, &TestWorker::allDone,
            this,   [this, worker](int total, int passed, int failed) {
                m_running = false;
                emit allTestsFinished(total, passed, failed);
                worker->deleteLater();
                m_workerThread->quit();
            });

    m_workerThread->start();

    // 在工作线程中调用 execute
    QMetaObject::invokeMethod(worker, [worker, list]() {
        worker->execute(list);
    }, Qt::QueuedConnection);
}
