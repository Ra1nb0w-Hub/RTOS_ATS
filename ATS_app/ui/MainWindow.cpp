#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "sdk/ats_printer.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QtGlobal>
#include <QAction>
#include <QPushButton>
#include <QToolBar>

static MainWindow *s_mainWindow = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_runner(new TestRunner(this))
    , m_logManager(LogManager::instance())
    , m_appThread(new AppThread(this))
    , m_qemuController(new QemuController(this))
    , m_serialServer(new RpcSerialServer(this))
{
    ui->setupUi(this);

    setupUi();
    setupToolBar();

    s_mainWindow = this;
    ats_printer_set_close_callback([]() {
        int w = 0, h = 0;
        const unsigned char *data = ats_printer_get_receipt_buffer(&w, &h);
        qDebug() << "[Receipt] close_callback fired, data:" << (void*)data << "w:" << w << "h:" << h;
        if (data && w > 0 && h > 0) {
            s_mainWindow->m_receiptPanel->setReceiptData(
                QByteArray(reinterpret_cast<const char *>(data), w * h), w, h);
            qDebug() << "[Receipt] Copied" << w * h << "bytes, invoking showReceipt";
            QMetaObject::invokeMethod(s_mainWindow->m_receiptPanel, "showReceipt", Qt::QueuedConnection);
        } else {
            qDebug() << "[Receipt] No valid buffer in callback!";
        }
    });

    connectSignals();

    m_logPanel->appendLog("=== ATS系统已启动 ===", "SYS");
    m_logPanel->appendLog(QString("启动时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "INFO");

    m_testCasesPanel->loadDefaultConfig();

    m_logManager->setOutputCallback([this](const QString &msg, const QString &level) {
        m_logPanel->appendLog(msg, level);
    });

    connect(m_serialServer, &RpcSerialServer::logMessage, m_logPanel, &LogPanel::appendLog);
    connect(m_qemuController, &QemuController::logMessage, m_logPanel, &LogPanel::appendLog);
    connect(m_qemuController, &QemuController::started, this, [this]() {
        m_appStarted = true;
        m_buttonsPanel->setAppStarted(true);
        m_statusPanel->setAppStarted(true);
        m_testCasesPanel->setAppStarted(true);
    });
    connect(m_qemuController, &QemuController::stopped, this, [this]() {
        m_appStarted = false;
        m_buttonsPanel->setAppStarted(false);
        m_statusPanel->setAppStarted(false);
        m_testCasesPanel->setAppStarted(false);
        if (m_closePending) {
            m_logPanel->appendLog("QEMU 已停止，继续关闭主窗口...", "SYS");
            m_closePending = false;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    });

    {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        auto resolvePort = [this, &env](const char *name, quint16 defaultPort) {
            bool portOk = false;
            quint16 port = env.value(QString::fromLatin1(name), QString::number(defaultPort)).toUShort(&portOk);
            if (!portOk || port == 0U) {
                m_logPanel->appendLog(QString("%1 无效，已回退到默认端口 %2").arg(QString::fromLatin1(name)).arg(defaultPort), "WARN");
                return defaultPort;
            }
            return port;
        };
        const quint16 serialPort = resolvePort("ATS_SERIAL_PORT", 34567U);

        if (!m_serialServer->start(serialPort)) {
            m_logPanel->appendLog(QString("串口服务启动失败, 端口: %1").arg(serialPort), "ERROR");
        }
    }

    m_screenPanel->startRendering();
    m_statusPanel->startMonitoring();
}

MainWindow::~MainWindow()
{
    if (m_qemuController != nullptr) {
        m_qemuController->stop();
    }
    m_logManager->setOutputCallback(nullptr);
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_qemuController != nullptr && m_qemuController->isRunning()) {
        if (!m_closePending) {
            m_closePending = true;
            m_logPanel->appendLog("主窗口关闭中，正在异步停止 QEMU...", "SYS");
            setEnabled(false);
            m_qemuController->stop();
        }

        event->ignore();
        return;
    }

    setEnabled(true);
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi()
{
    m_screenPanel = new ScreenPanel(this);
    m_buttonsPanel = new ButtonsPanel(this);
    m_testCasesPanel = new TestCasesPanel(this);
    m_statusPanel = new StatusPanel(this);
    m_logPanel = new LogPanel(this);
    m_receiptPanel = new ReceiptPanel(this);

    ui->verticalLayout_screenHost->addWidget(m_screenPanel);
    ui->verticalLayout_deviceButtonsHost->addWidget(m_buttonsPanel);
    ui->verticalLayout_testCasesHost->addWidget(m_testCasesPanel);
    ui->verticalLayout_statusHost->addWidget(m_statusPanel);
    ui->verticalLayout_receipt->addWidget(m_receiptPanel);
    ui->verticalLayout_logHost->addWidget(m_logPanel);
    m_receiptPanel->ui()->groupBoxReceipt->setMinimumWidth(300);
    m_receiptPanel->ui()->groupBoxReceipt->setMaximumWidth(QWIDGETSIZE_MAX);
}

void MainWindow::setupToolBar()
{
    QToolBar *toolBar = addToolBar(QStringLiteral("工具栏"));
    toolBar->setObjectName(QStringLiteral("AtsToolBar"));
    toolBar->setMovable(false);

    m_actionImportElf = toolBar->addAction(QStringLiteral("导入ELF文件"));
}

void MainWindow::connectSignals()
{
    connect(m_testCasesPanel->ui()->btnImportConfig, &QPushButton::clicked, this, &MainWindow::onImportConfig);
    connect(m_actionImportElf, &QAction::triggered, this, &MainWindow::onImportElf);

    connect(m_testCasesPanel, &TestCasesPanel::runScriptsRequested, this, [this](const QMap<QString, QString> &scripts) {
        if (scripts.isEmpty()) {
            m_logPanel->appendLog("没有选择测试的用例", "WARN");
            return;
        }
        m_testCasesPanel->setRunning(true);
        m_logPanel->appendLog(QString("开始执行测试用例, 总计：%1个").arg(scripts.size()), "INFO");
        m_runner->runScripts(scripts);
    });

    connect(m_testCasesPanel, &TestCasesPanel::stopRequested, this, [this]() {
        m_logPanel->appendLog("停止用例测试", "WARN");
        m_testCasesPanel->ui()->btnStop->setEnabled(false);
        m_runner->stop();
    });

    connect(m_testCasesPanel, &TestCasesPanel::logMessage, m_logPanel, &LogPanel::appendLog);

    connect(m_runner, &TestRunner::testStarted,      this, &MainWindow::onTestStarted);
    connect(m_runner, &TestRunner::testFinished,     this, &MainWindow::onTestFinished);
    connect(m_runner, &TestRunner::allTestsFinished, this, &MainWindow::onAllTestsFinished);
    connect(m_runner, &TestRunner::progressChanged,  this, &MainWindow::onProgressChanged);

    connect(m_buttonsPanel, &ButtonsPanel::startAppRequested, this, [this]() {
        if (m_importedElfPath.isEmpty()) {
            m_appStarted = true;
            m_buttonsPanel->setAppStarted(true);
            m_statusPanel->setAppStarted(true);
            m_testCasesPanel->setAppStarted(true);
            m_appThread->startApp();
        } else {
            startQemuWithImportedElf();
        }
    });
}

void MainWindow::startQemuWithImportedElf()
{
    if (m_importedElfPath.isEmpty()) {
        m_logPanel->appendLog("请先导入 ELF 文件。", "WARN");
        return;
    }

    if (!m_serialServer->isListening()) {
        m_logPanel->appendLog("串口服务未监听，无法启动应用程序。", "ERROR");
        return;
    }

    if (m_qemuController->isRunning()) {
        m_logPanel->appendLog("QEMU 已在运行中。", "WARN");
        return;
    }

    m_qemuController->setFirmwarePath(m_importedElfPath);
    if (!m_qemuController->start(m_serialServer->listenPort())) {
        m_logPanel->appendLog("QEMU 启动失败。", "ERROR");
    }
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MainWindow::onTestStarted(const QString &caseName)
{
    m_testCasesPanel->updateStatus(caseName, "Running");
}

void MainWindow::onTestFinished(const QString &caseName, bool passed, const QString &detail)
{
    Q_UNUSED(detail)
    QString status = passed ? "PASS" : "FAIL";
    m_testCasesPanel->updateStatus(caseName, status);
}

void MainWindow::onAllTestsFinished(int total, int passed, int failed)
{
    Q_UNUSED(total)
    Q_UNUSED(passed)
    Q_UNUSED(failed)
    m_testCasesPanel->setRunning(false);
}

void MainWindow::onProgressChanged(int current, int total)
{
    Q_UNUSED(current)
    Q_UNUSED(total)
}

void MainWindow::onImportConfig()
{
    if (m_appStarted) {
        QMessageBox::information(this, "提示", "程序运行中，无法导入配置文件");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, "导入配置文件",
        QDir::currentPath(),
        "XML Files (*.xml);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    m_testCasesPanel->loadConfigFromFile(filePath);
}

void MainWindow::onImportElf()
{
    static const char kRecentElfDirectoryKey[] = "paths/recentElfDirectory";

    if (m_appStarted || m_qemuController->isRunning()) {
        QMessageBox::information(this, "提示", "应用程序运行中，无法重新导入 ELF 文件");
        return;
    }

    QSettings settings;
    QString initialDirectory = settings.value(QString::fromLatin1(kRecentElfDirectoryKey)).toString();
    if (initialDirectory.isEmpty()) {
        initialDirectory = m_importedElfPath.isEmpty()
                               ? QDir::homePath()
                               : QFileInfo(m_importedElfPath).absolutePath();
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "导入 ELF 文件",
        initialDirectory,
        "ELF Files (*.elf *.axf *.out);;All Files (*)");

    if (filePath.isEmpty()) {
        return;
    }

    const QFileInfo elfInfo(filePath);
    if (!elfInfo.exists() || !elfInfo.isFile()) {
        QMessageBox::warning(this, "导入失败", QString("无法打开 ELF 文件: %1").arg(filePath));
        return;
    }

    m_importedElfPath = elfInfo.absoluteFilePath();
    settings.setValue(QString::fromLatin1(kRecentElfDirectoryKey), elfInfo.absolutePath());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}
