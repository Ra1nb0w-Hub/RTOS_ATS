#pragma once

#include <QMainWindow>
#include "ScreenPanel.h"
#include "ButtonsPanel.h"
#include "TestCasesPanel.h"
#include "StatusPanel.h"
#include "LogPanel.h"
#include "ReceiptPanel.h"
#include "core/TestRunner.h"
#include "core/AppThread.h"
#include "log/LogManager.h"
#include "core/QemuController.h"
#include "core/RpcSerialServer.h"
#include "core/RpcFrameProcessor.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_printer.h"

class QAction;
class QCloseEvent;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onTestStarted(const QString &caseName);
    void onTestFinished(const QString &caseName, bool passed, const QString &detail);
    void onAllTestsFinished(int total, int passed, int failed);
    void onProgressChanged(int current, int total);

    void onImportConfig();
    void onImportElf();

private:
    void setupUi();
    void setupToolBar();
    void connectSignals();
    void startQemuWithImportedElf();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    Ui::MainWindow *ui;
    TestRunner     *m_runner;
    LogManager     *m_logManager;
    AppThread      *m_appThread;
    QemuController *m_qemuController;
    RpcSerialServer *m_serialServer;
    ScreenPanel    *m_screenPanel = nullptr;
    ButtonsPanel   *m_buttonsPanel = nullptr;
    TestCasesPanel *m_testCasesPanel = nullptr;
    StatusPanel    *m_statusPanel = nullptr;
    LogPanel       *m_logPanel = nullptr;
    ReceiptPanel   *m_receiptPanel = nullptr;

    bool         m_appStarted = false;
    bool         m_closePending = false;
    QString      m_importedElfPath;
    QAction     *m_actionImportElf = nullptr;
};
