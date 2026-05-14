#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "api/ats_lcd.h"
#include "api/ats_printer.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QTextStream>
#include <QScrollBar>
#include <QDebug>
#include <QResizeEvent>
#include <QPainter>
#include <QTimer>
#include <QListWidgetItem>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

static MainWindow *s_mainWindow = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_runner(new TestRunner(this))
    , m_logManager(LogManager::instance())
    , m_appThread(new AppThread(this))
{
    ui->setupUi(this);

    // 设置主水平布局 stretch：左列(屏幕/按键/用例)=0，中列(状态/小票)=0，日志列=1
    // 拖拽窗口变宽时，多余空间全部分配给日志区
    ui->horizontalLayout_main->setStretchFactor(ui->verticalLayout_left, 0);
    ui->horizontalLayout_main->setStretchFactor(ui->verticalLayout_middleRight, 0);
    ui->horizontalLayout_main->setStretchFactor(ui->groupBoxLog, 1);

    setupUi();

    // 注册小票关闭回调（从 AppThread 中触发，同步拷贝 buffer 后异步显示）
    s_mainWindow = this;
    ats_printer_set_close_callback([]() {
        int w = 0, h = 0;
        const unsigned char *data = ats_printer_get_receipt_buffer(&w, &h);
        qDebug() << "[Receipt] close_callback fired, data:" << (void*)data << "w:" << w << "h:" << h;
        if (data && w > 0 && h > 0) {
            // 在子线程中同步拷贝，避免 OPay 紧接着 open() 覆盖 buffer
            s_mainWindow->m_receiptData = QByteArray(
                reinterpret_cast<const char *>(data), w * h);
            s_mainWindow->m_receiptWidth = w;
            s_mainWindow->m_receiptHeight = h;
            qDebug() << "[Receipt] Copied" << w * h << "bytes, invoking onShowReceipt";
            QMetaObject::invokeMethod(s_mainWindow, "onShowReceipt", Qt::QueuedConnection);
        } else {
            qDebug() << "[Receipt] No valid buffer in callback!";
        }
    });

    // 禁用最大化按钮，仅允许水平方向调整窗口宽度，高度保持固定
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    int fixedH = size().height();
    setMinimumHeight(fixedH);
    setMaximumHeight(fixedH);
    setMinimumWidth(size().width());
    connectSignals();

    appendLog("=== ATS系统已启动 ===", "SYS");
    appendLog(QString("启动时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "INFO");
    
    // 加载默认配置文件
    loadDefaultConfig();

    // 将日志管理器与窗口绑定
    m_logManager->setOutputCallback([this](const QString &msg, const QString &level) {
        appendLog(msg, level);
    });

    /* 等待用户按下电源键启动 ats_main */
    appendLog("点击[电源]键启动App...", "SYS");

    /* 屏幕刷新定时器（启动后立即开始，ats_main 同时在后台运行） */
    connect(&m_screenTimer, &QTimer::timeout, this, &MainWindow::updateScreenDisplay);
    m_screenTimer.start(50);

    /* 日志批量刷新定时器（50ms 防抖，合并高频日志减少 UI 重绘） */
    m_logFlushTimer.setSingleShot(true);
    m_logFlushTimer.setInterval(50);
    connect(&m_logFlushTimer, &QTimer::timeout, this, &MainWindow::flushPendingLogs);

    /* 初始显示占位 */
    updateScreenDisplay();

    /* 状态面板定时刷新（500ms），首次延迟 200ms 等待窗口完全就绪） */
    m_statusTimer.setInterval(500);
    connect(&m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusPanel);
    QTimer::singleShot(200, this, [this]() {
        m_statusTimer.start();
        updateStatusPanel();
    });
}

MainWindow::~MainWindow()
{
    m_logManager->setOutputCallback(nullptr);
    delete ui;
}

void MainWindow::setupUi()
{
    /* 小票区域控件已在 MainWindow.ui 中静态定义，此处无需额外初始化 */
}

void MainWindow::connectSignals()
{
    // 按钮
    connect(ui->btnRunSelected, &QPushButton::clicked, this, &MainWindow::onRunSelected);
    connect(ui->btnRunAll,      &QPushButton::clicked, this, &MainWindow::onRunAll);
    connect(ui->btnStop,        &QPushButton::clicked, this, &MainWindow::onStop);
    connect(ui->btnSaveLog,     &QPushButton::clicked, this, &MainWindow::onSaveLog);
    connect(ui->btnClearLog,    &QPushButton::clicked, this, &MainWindow::onClearLog);
    connect(ui->btnImportConfig, &QPushButton::clicked, this, &MainWindow::onImportConfig);

    // 日志过滤
    connect(ui->comboBoxLogLevel, &QComboBox::currentTextChanged,
            this, &MainWindow::onLogLevelChanged);
    connect(ui->lineEditLogFilter, &QLineEdit::textChanged,
            this, &MainWindow::onLogFilterChanged);

    // 搜索测试用例
    connect(ui->lineEditSearch, &QLineEdit::textChanged,
            this, &MainWindow::onSearchCases);

    // 点击测试用例列表项切换checkbox状态
    connect(ui->listWidgetCases, &QListWidget::itemClicked,
            this, &MainWindow::onCaseItemClicked);

    connect(ui->btnDevicePower, &QPushButton::pressed, this, [this]() {
        if (m_appStarted) {
            // app 已启动：电源键按下正常发送按键事件
            onDeviceButtonPressed(ATS_KEY_CODE_POWER);
        }
        // app 未启动：按下时不发送任何事件，等待抬起触发启动
    });
    connect(ui->btnDevicePower, &QPushButton::released, this, [this]() {
        if (!m_appStarted) {
            // 首次按下并抬起电源键：启动 ats_main
            m_appStarted = true;
            m_appThread->startApp();
        } else {
            // app 已启动：电源键抬起正常发送释放事件
            onDeviceButtonReleased(ATS_KEY_CODE_NONE);
        }
    });

    connect(ui->btnDeviceVolUp, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_VOLUME_INC);
    });
    connect(ui->btnDeviceVolUp, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(ui->btnDeviceMenu, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_MENU);
    });
    connect(ui->btnDeviceMenu, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(ui->btnDeviceVolDown, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_VOLUME_DEC);
    });
    connect(ui->btnDeviceVolDown, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(ui->btnDeviceReplay, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_REPLAY);
    });
    connect(ui->btnDeviceReplay, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    // TestRunner 信号
    connect(m_runner, &TestRunner::testStarted,      this, &MainWindow::onTestStarted);
    connect(m_runner, &TestRunner::testFinished,     this, &MainWindow::onTestFinished);
    connect(m_runner, &TestRunner::allTestsFinished, this, &MainWindow::onAllTestsFinished);
    connect(m_runner, &TestRunner::progressChanged,  this, &MainWindow::onProgressChanged);
}

void MainWindow::appendLog(const QString &text, const QString &level)
{
    // 根据级别着色（HTML 富文本）
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    static const QMap<QString, QString> colorMap = {
                                                    {"DEBUG",   "#6a9fb5"},
                                                    {"INFO",    "#c9d1d9"},
                                                    {"WARN",    "#e6c07b"},
                                                    {"ERROR",   "#e06c75"},
                                                    {"PASS",    "#98c379"},
                                                    {"FAIL",    "#e06c75"},
                                                    {"SYS",     "#61afef"},
                                                    };

    QString color = colorMap.value(level.toUpper(), "#c9d1d9");
    QString escapedText = text.toHtmlEscaped().replace("\r\n", "<br>").replace("\n", "<br>");
    QString coloredText = QString("<span style='color:#666;'>[%1]</span> "
                                  "<span style='color:%2;font-weight:bold;'>[%3]</span> "
                                  "<span style='color:%2;'>%4</span>")
                              .arg(timestamp, color, level.toUpper(), escapedText);


    // 始终存储所有日志
    m_logEntries.append({coloredText, level.toUpper(), text});

    // 仅当匹配当前过滤条件时才加入待刷新队列
    QString filterText = ui->lineEditLogFilter->text();
    if (!filterText.isEmpty() && !text.contains(filterText, Qt::CaseInsensitive))
        return;

    QString levelFilter = ui->comboBoxLogLevel->currentText();
    if (levelFilter != "ALL" && level.toUpper() != levelFilter)
        return;

    m_pendingLogHtml.append(coloredText);

    // 启动防抖定时器，50ms 内的日志会被合并为一次 UI 更新
    if (!m_logFlushScheduled) {
        m_logFlushScheduled = true;
        m_logFlushTimer.start();
    }
}

void MainWindow::flushPendingLogs()
{
    m_logFlushScheduled = false;
    if (m_pendingLogHtml.isEmpty())
        return;

    // 一次性追加所有待刷新的日志
    QString htmlBlock = m_pendingLogHtml.join("<br>");
    m_pendingLogHtml.clear();

    ui->plainTextEditLog->append(htmlBlock);

    if (ui->checkBoxAutoScroll->isChecked()) {
        auto *scrollbar = ui->plainTextEditLog->verticalScrollBar();
        if (scrollbar != nullptr)
            scrollbar->setValue(scrollbar->maximum());
    }
}

void MainWindow::rebuildLogView()
{
    ui->plainTextEditLog->clear();

    QString levelFilter = ui->comboBoxLogLevel->currentText();
    QString filterText  = ui->lineEditLogFilter->text();

    QStringList visibleEntries;
    for (const auto &entry : m_logEntries) {
        if (!filterText.isEmpty() && !entry.text.contains(filterText, Qt::CaseInsensitive))
            continue;
        if (levelFilter != "ALL" && entry.level != levelFilter)
            continue;

        visibleEntries.append(entry.html);
    }

    if (!visibleEntries.isEmpty()) {
        // 一次性追加所有可见日志，避免多次重绘
        ui->plainTextEditLog->append(visibleEntries.join("<br>"));
    }

    if (ui->checkBoxAutoScroll->isChecked()) {
        auto *scrollbar = ui->plainTextEditLog->verticalScrollBar();
        if (scrollbar != nullptr)
            scrollbar->setValue(scrollbar->maximum());
    }
}

void MainWindow::updateTestCaseStatus(const QString &caseName, const QString &status)
{
    for (int i = 0; i < ui->listWidgetCases->count(); ++i) {
        auto *item = ui->listWidgetCases->item(i);
        // 使用 name 匹配（UserRole 存储的是 name）
        if (item->data(Qt::UserRole).toString() == caseName) {
            QString showName = item->data(Qt::UserRole).toString();
            int idx = item->data(Qt::UserRole + 2).toInt();
            QString displayText = QString("%1.[%2]%3").arg(idx).arg(status).arg(showName);
            item->setText(displayText);
            QFont itemFont("Consolas", 10, QFont::DemiBold);
            item->setFont(itemFont);
            if (status == "PASS") {
                item->setForeground(QColor("#7dcea0"));  // 鲜明绿色
            } else if (status == "FAIL") {
                item->setForeground(QColor("#f1948a"));  // 鲜明红色
            } else if (status == "Running") {
                item->setForeground(QColor("#ff8f40"));  // 亮橙色，高对比
            }
            break;
        }
    }
}

void MainWindow::setRunning(bool running)
{
    ui->btnRunSelected->setEnabled(!running);
    ui->btnRunAll->setEnabled(!running);
    ui->btnStop->setEnabled(running);
    ui->listWidgetCases->setEnabled(!running);
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MainWindow::onRunSelected()
{
    // 收集选中的用例名称和对应的脚本路径（仅根据 checkbox 勾选状态判断）
    QMap<QString, QString> selectedMap;
    for (int i = 0; i < ui->listWidgetCases->count(); ++i) {
        auto *item = ui->listWidgetCases->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->data(Qt::UserRole).toString();
            QString script = item->data(Qt::UserRole + 1).toString();
            if (!script.isEmpty()) {
                selectedMap.insert(name, script);
            }
        }
    }
    if (selectedMap.isEmpty()) {
        appendLog("没有选择测试的用例", "WARN");
        return;
    }
    setRunning(true);
    appendLog(QString("开始执行已选择的测试用例, 总计：%1个").arg(selectedMap.size()), "INFO");
    m_runner->runScripts(selectedMap);
}

void MainWindow::onRunAll()
{
    // 收集所有用例的名称和脚本路径
    QMap<QString, QString> allMap;
    for (int i = 0; i < ui->listWidgetCases->count(); ++i) {
        auto *item = ui->listWidgetCases->item(i);
        QString name = item->data(Qt::UserRole).toString();
        QString script = item->data(Qt::UserRole + 1).toString();
        if (!script.isEmpty()) {
            allMap.insert(name, script);
        }
    }
    if (allMap.isEmpty()) {
        appendLog("没有选择测试的用例", "WARN");
        return;
    }
    setRunning(true);
    appendLog(QString("开始执行全部的测试用例, 总计：%1个").arg(allMap.size()), "INFO");
    m_runner->runScripts(allMap);
}

void MainWindow::onStop()
{
    m_runner->stop();
    appendLog("停止用例测试", "WARN");
    // 不立即调用 setRunning(false)，等待 allDone 信号由 onAllTestsFinished 处理
    // 只禁用 Stop 按钮，防止重复点击；Run/RunAll 保持禁用直到真正停止
    ui->btnStop->setEnabled(false);
}

void MainWindow::onTestStarted(const QString &caseName)
{
    updateTestCaseStatus(caseName, "Running");
}

void MainWindow::onTestFinished(const QString &caseName, bool passed, const QString &detail)
{
    Q_UNUSED(detail)
    QString status = passed ? "PASS" : "FAIL";
    updateTestCaseStatus(caseName, status);
}

void MainWindow::onAllTestsFinished(int total, int passed, int failed)
{
    Q_UNUSED(total)
    Q_UNUSED(passed)
    Q_UNUSED(failed)
    setRunning(false);
}

void MainWindow::onProgressChanged(int current, int total)
{
    Q_UNUSED(current)
    Q_UNUSED(total)
}

void MainWindow::onSaveLog()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Save Log", QString("ats_log_%1.txt")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&file);
        ts << ui->plainTextEditLog->toPlainText();
        appendLog(QString("Log saved to: %1").arg(path), "INFO");
    } else {
        appendLog(QString("Failed to save log: %1").arg(path), "ERROR");
    }
}

void MainWindow::onClearLog()
{
    ui->plainTextEditLog->clear();
    m_logEntries.clear();
}

void MainWindow::onLogLevelChanged(const QString &/*level*/)
{
    rebuildLogView();
}

void MainWindow::onLogFilterChanged(const QString &/*filter*/)
{
    rebuildLogView();
}

void MainWindow::onSearchCases(const QString &text)
{
    for (int i = 0; i < ui->listWidgetCases->count(); ++i) {
        auto *item = ui->listWidgetCases->item(i);
        bool match = text.isEmpty() ||
                     item->data(Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void MainWindow::onCaseItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    
    // 切换checkbox状态
    Qt::CheckState newState = (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
    item->setCheckState(newState);
    
    // 同步更新配置中的 chosen 状态（用 script 文件名匹配）
    QString scriptBaseName = item->data(Qt::UserRole).toString();
    for (auto &config : m_testCaseConfigs) {
        QString configScriptBaseName;
        if (!config.script.isEmpty()) {
            configScriptBaseName = QFileInfo(config.script).completeBaseName();
        } else {
            configScriptBaseName = config.name;  // 如果没有 script，用 name 作为 fallback
        }
        if (configScriptBaseName == scriptBaseName) {
            config.chosen = (newState == Qt::Checked);
            break;
        }
    }
}

// ─── 配置导入相关实现 ─────────────────────────────────────────────────────────

void MainWindow::onImportConfig()
{
    // 程序运行后禁止重新加载配置
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
    
    if (loadConfigFromFile(filePath)) {
        m_currentConfigPath = filePath;
    }
}

bool MainWindow::loadConfigFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLog(QString("无法打开配置文件: %1").arg(filePath), "ERROR");
        QMessageBox::warning(this, "导入失败", QString("无法打开文件: %1").arg(filePath));
        return false;
    }
    
    QDomDocument doc;
    const auto parseResult = doc.setContent(&file);
    if (!parseResult) {
        file.close();
        QString err = QString("XML解析错误: %1 (行%2, 列%3)")
                          .arg(parseResult.errorMessage)
                          .arg(parseResult.errorLine)
                          .arg(parseResult.errorColumn);
        appendLog(err, "ERROR");
        QMessageBox::warning(this, "导入失败", err);
        return false;
    }
    file.close();
    
    QDomElement root = doc.documentElement();
    if (root.tagName() != "config") {
        appendLog("配置文件格式错误: 根元素必须是 <config>", "ERROR");
        QMessageBox::warning(this, "导入失败", "配置文件格式错误: 根元素必须是 <config>");
        return false;
    }
    
    // ─── 解析 <default> 节点（设备默认配置） ───────────────────────────────
    QDomElement defaultEl = root.firstChildElement("default");
    if (!defaultEl.isNull()) {
        DeviceConfig deviceConfig;
        
        QDomElement snEl = defaultEl.firstChildElement("device_serial_number");
        if (!snEl.isNull()) {
            deviceConfig.serialNumber = snEl.text().trimmed();
        }
        
        QDomElement modeEl = defaultEl.firstChildElement("device_net_mode");
        if (!modeEl.isNull()) {
            deviceConfig.netMode = modeEl.text().trimmed();
        }
        
        QDomElement ssidEl = defaultEl.firstChildElement("device_net_ssid");
        if (!ssidEl.isNull()) {
            deviceConfig.netSsid = ssidEl.text().trimmed();
        }

        QDomElement wifiSignalEl = defaultEl.firstChildElement("device_net_wifi_signal");
        if (!wifiSignalEl.isNull()) {
            deviceConfig.wifiSignal = wifiSignalEl.text().trimmed().toInt();
        }

        QDomElement mccEl = defaultEl.firstChildElement("device_net_celluar_mcc");
        if (!mccEl.isNull()) {
            deviceConfig.cellularMcc = mccEl.text().trimmed().toInt();
        }
        
        QDomElement mncEl = defaultEl.firstChildElement("device_net_celluar_mnc");
        if (!mncEl.isNull()) {
            deviceConfig.cellularMnc = mncEl.text().trimmed().toInt();
        }
        
        QDomElement lacEl = defaultEl.firstChildElement("device_net_celluar_lac");
        if (!lacEl.isNull()) {
            deviceConfig.cellularLac = lacEl.text().trimmed().toInt();
        }
        
        QDomElement cidEl = defaultEl.firstChildElement("device_net_celluar_cid");
        if (!cidEl.isNull()) {
            deviceConfig.cellularCid = cidEl.text().trimmed().toInt();
        }
        
        QDomElement signalEl = defaultEl.firstChildElement("device_net_cellular_signal");
        if (!signalEl.isNull()) {
            deviceConfig.cellularSignal = signalEl.text().trimmed().toInt();
        }
        
        QDomElement imeiEl = defaultEl.firstChildElement("device_net_cellular_imei");
        if (!imeiEl.isNull()) {
            deviceConfig.cellularImei = imeiEl.text().trimmed();
        }
        
        QDomElement imsiEl = defaultEl.firstChildElement("device_net_cellular_imsi");
        if (!imsiEl.isNull()) {
            deviceConfig.cellularImsi = imsiEl.text().trimmed();
        }

        QDomElement apListEl = defaultEl.firstChildElement("device_net_ap_list");
        if (!apListEl.isNull()) {
            QDomNodeList apNodes = apListEl.elementsByTagName("ap");
            for (int i = 0; i < apNodes.count(); ++i) {
                QDomElement apEl = apNodes.at(i).toElement();
                if (apEl.isNull()) continue;
                
                WifiApInfo ap;
                QDomElement apSsidEl = apEl.firstChildElement("ssid");
                if (!apSsidEl.isNull()) ap.ssid = apSsidEl.text().trimmed();
                
                QDomElement apMacEl = apEl.firstChildElement("mac");
                if (!apMacEl.isNull()) ap.mac = apMacEl.text().trimmed();
                
                QDomElement apSignalEl = apEl.firstChildElement("signal");
                if (!apSignalEl.isNull()) ap.signal = apSignalEl.text().trimmed().toInt();
                
                if (!ap.ssid.isEmpty()) {
                    deviceConfig.wifiApList.append(ap);
                }
            }
        }
        
        applyDeviceConfig(deviceConfig);
    }
    
    // ─── 解析 <cases> 节点（测试用例配置） ─────────────────────────────
    QDomElement casesEl = root.firstChildElement("cases");
    if (!casesEl.isNull()) {
        QVector<TestCaseConfig> newConfigs;
        QDomNodeList caseNodes = casesEl.elementsByTagName("case");

        for (int i = 0; i < caseNodes.count(); ++i) {
            QDomElement caseEl = caseNodes.at(i).toElement();
            if (caseEl.isNull()) continue;

            TestCaseConfig cfg;

            // name（测试用例名称，必需，作为唯一标识）
            cfg.name = caseEl.attribute("name").trimmed();
            if (cfg.name.isEmpty()) {
                cfg.name = caseEl.firstChildElement("name").text().trimmed();
            }
            if (cfg.name.isEmpty()) continue;

            // script（脚本路径，用于检查文件是否存在）
            cfg.script = caseEl.attribute("script").trimmed();
            if (cfg.script.isEmpty()) {
                cfg.script = caseEl.firstChildElement("script").text().trimmed();
            }

            // enabled（可选，默认 true）- 控制是否出现在测试列表中
            QString enabledStr = caseEl.attribute("enabled").trimmed().toLower();
            if (enabledStr.isEmpty()) {
                enabledStr = caseEl.firstChildElement("enabled").text().trimmed().toLower();
            }
            cfg.enabled = (enabledStr != "false" && enabledStr != "0");

            // chosen（可选，默认 false）- 控制是否默认勾选
            QString chosenStr = caseEl.attribute("chosen").trimmed().toLower();
            if (chosenStr.isEmpty()) {
                chosenStr = caseEl.firstChildElement("chosen").text().trimmed().toLower();
            }
            cfg.chosen = (chosenStr == "true" || chosenStr == "1");

            // 保存所有属性为额外参数
            QDomNamedNodeMap attrs = caseEl.attributes();
            for (int j = 0; j < attrs.count(); ++j) {
                QDomAttr attr = attrs.item(j).toAttr();
                cfg.params.insert(attr.name(), attr.value());
            }

            newConfigs.append(cfg);
        }

        if (newConfigs.isEmpty()) {
            appendLog("配置文件中没有有效的测试用例", "WARN");
            QMessageBox::warning(this, "导入失败", "配置文件中没有有效的测试用例");
            return false;
        }

        // 提取 XML 文件所在目录，用于解析相对路径的脚本
        QString xmlDir = QFileInfo(filePath).absolutePath();
        applyTestCaseConfig(newConfigs, xmlDir);
    }

    return true;
}

void MainWindow::applyTestCaseConfig(const QVector<TestCaseConfig> &configs, const QString &xmlDir)
{
    appendLog(QString("发现 %1 个测试用例配置，正在导入中...").arg(configs.size()), "SYS");
    
    // 清空现有列表和配置
    ui->listWidgetCases->clear();
    m_testCaseConfigs.clear();
    
    QFont itemFont("Consolas", 10);
    QColor itemForeground("#82aaff");
    int caseIndex = 1;
    
    for (const auto &config : configs) {
        // 跳过 enabled=false 的用例
        if (!config.enabled) {
            appendLog(QString("测试用例已禁用，跳过[%1]").arg(config.name), "DEBUG");
            continue;
        }
        
        // 解析脚本路径（基于 XML 目录处理相对路径）
        QString scriptFullPath;
        if (!config.script.isEmpty()) {
            QDir xmlDirectory(xmlDir);
            scriptFullPath = xmlDirectory.absoluteFilePath(config.script);

            if (!QFile::exists(scriptFullPath)) {
                appendLog(QString("测试用例脚本文件不存在，跳过[%1]")
                              .arg(config.name), "WARN");
                continue;
            }
        }
        
        // 创建列表项
        auto *item = new QListWidgetItem(
            QString("%1.%2").arg(caseIndex).arg(config.name), ui->listWidgetCases);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(config.chosen ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, config.name);           // name 作为唯一标识
        item->setData(Qt::UserRole + 1, scriptFullPath);    // 脚本完整路径
        item->setData(Qt::UserRole + 2, caseIndex);         // 序号
        item->setFont(itemFont);
        item->setForeground(itemForeground);
        
        // 保存到内存配置（使用完整路径）
        TestCaseConfig savedConfig = config;
        savedConfig.script = scriptFullPath;
        m_testCaseConfigs.append(savedConfig);
        
        appendLog(QString("测试用例已加载: [%1]")
                      .arg(config.name), "INFO");
        
        caseIndex++;
    }

    appendLog(QString("已成功导入 %1 个测试用例配置").arg(ui->listWidgetCases->count()), "SYS");
}

void MainWindow::loadDefaultConfig()
{
    // 清除保存的测试用例
    ui->listWidgetCases->clear();
    m_testCaseConfigs.clear();
    
    // 默认配置文件路径: 运行exe程序的根目录/scripts/config.xml
    QString exePath = QCoreApplication::applicationDirPath();
    QString defaultPath = QDir(exePath).absoluteFilePath("scripts/config.xml");
    
    QFileInfo fileInfo(defaultPath);
    if (fileInfo.exists()) {
        appendLog("发现默认配置文件, 正在加载...", "SYS");
        if (loadConfigFromFile(defaultPath)) {
            m_currentConfigPath = defaultPath;
            appendLog("默认配置文件已加载完成", "SYS");
        }
    }
}

void MainWindow::applyDeviceConfig(const DeviceConfig &config)
{
    // 保存设备配置到成员变量
    m_deviceConfig = config;
    
    // 设置设备序列号
    if (!config.serialNumber.isEmpty()) {
        QByteArray snBytes = config.serialNumber.toUtf8();
        ats_serial_number_set(snBytes.data());
        appendLog(QString("设备序列号已设置: %1").arg(config.serialNumber), "INFO");
    }
    
    // 设置网络模式
    if (!config.netMode.isEmpty()) {
        ats_net_mode_t mode = ATS_NET_MODE_CELLUALR; // 默认蜂窝网络
        if (config.netMode == "wifi") {
            mode = ATS_NET_MODE_WIFI;
        } else if (config.netMode == "ethernet") {
            mode = ATS_NET_MODE_ETHERNET;
        } else if (config.netMode == "cellular") {
            mode = ATS_NET_MODE_CELLUALR;
        }
        ats_net_set_mode(mode);
        appendLog(QString("网络模式已设置: %1").arg(config.netMode), "INFO");
    }
    
    // 设置WiFi SSID
    if (!config.netSsid.isEmpty()) {
        QByteArray ssidBytes = config.netSsid.toUtf8();
        ats_net_wifi_set_ssid(ssidBytes.data());
        appendLog(QString("WiFi SSID已设置: %1").arg(config.netSsid), "INFO");
    }
    
    // 设置WiFi信号强度
    ats_net_wifi_set_signal(config.wifiSignal);
    appendLog(QString("WiFi信号强度已设置: %1").arg(config.wifiSignal), "INFO");
    
    // 设置蜂窝网络参数（可选）
    if (config.cellularMcc != 0) {
        ats_net_cellular_set_mcc(config.cellularMcc);
        appendLog(QString("蜂窝MCC已设置: %1").arg(config.cellularMcc), "INFO");
    }
    
    if (config.cellularMnc != 0) {
        ats_net_cellular_set_mnc(config.cellularMnc);
        appendLog(QString("蜂窝MNC已设置: %1").arg(config.cellularMnc), "INFO");
    }
    
    if (config.cellularLac != 0) {
        ats_net_cellular_set_lac(config.cellularLac);
        appendLog(QString("蜂窝LAC已设置: %1").arg(config.cellularLac), "INFO");
    }
    
    if (config.cellularCid != 0) {
        ats_net_cellular_set_cell_id(config.cellularCid);
        appendLog(QString("蜂窝CID已设置: %1").arg(config.cellularCid), "INFO");
    }
    
    if (config.cellularSignal != 0) {
        ats_net_cellular_set_signal(config.cellularSignal);
        appendLog(QString("蜂窝信号强度已设置: %1").arg(config.cellularSignal), "INFO");
    }
    
    if (!config.cellularImei.isEmpty()) {
        QByteArray imeiBytes = config.cellularImei.toUtf8();
        ats_net_cellular_set_imei(imeiBytes.data());
        appendLog(QString("蜂窝IMEI已设置: %1").arg(config.cellularImei), "INFO");
    }
    
    if (!config.cellularImsi.isEmpty()) {
        QByteArray imsiBytes = config.cellularImsi.toUtf8();
        ats_net_cellular_set_imsi(imsiBytes.data());
        appendLog(QString("蜂窝IMSI已设置: %1").arg(config.cellularImsi), "INFO");
    }
    
    // 设置WiFi AP列表（可选）
    if (!config.wifiApList.isEmpty()) {
        QVector<ats_net_wifi_ap_t> apArray;
        for (const auto &ap : config.wifiApList) {
            ats_net_wifi_ap_t atsAp;
            strncpy(atsAp.ssid, ap.ssid.toUtf8().constData(), sizeof(atsAp.ssid) - 1);
            atsAp.ssid[sizeof(atsAp.ssid) - 1] = '\0';
            strncpy(atsAp.mac, ap.mac.toUtf8().constData(), sizeof(atsAp.mac) - 1);
            atsAp.mac[sizeof(atsAp.mac) - 1] = '\0';
            atsAp.rssi = ap.signal;
            apArray.append(atsAp);
        }
        ats_net_wifi_set_ap_list(apArray.data(), static_cast<unsigned int>(apArray.size()));
        appendLog(QString("WiFi AP列表已设置: %1个AP").arg(config.wifiApList.size()), "INFO");
    }
}

// ─── 设备物理按键 Slots ──────────────────────────────────────────────────────

void MainWindow::onDeviceButtonPressed(int keyCode)
{
    ats_keypad_set_event(static_cast<uint8_t>(keyCode), true);
}

void MainWindow::onDeviceButtonReleased(int keyCode)
{
    ats_keypad_set_event(static_cast<uint8_t>(keyCode), false);
}

// ─── 小票预览 ──────────────────────────────────────────────────────────────────

void MainWindow::onShowReceipt()
{
    qDebug() << "[Receipt] onShowReceipt called, data size:" << m_receiptData.size()
             << "width:" << m_receiptWidth << "height:" << m_receiptHeight;

    if (m_receiptData.isEmpty() || m_receiptWidth <= 0 || m_receiptHeight <= 0) {
        qDebug() << "[Receipt] No valid data, skipping";
        return;
    }

    /* 从本地副本转为 QImage */
    m_receiptImage = grayToQImage(
        reinterpret_cast<const unsigned char *>(m_receiptData.constData()),
        m_receiptWidth, m_receiptHeight);
    if (m_receiptImage.isNull()) {
        qDebug() << "[Receipt] grayToQImage returned null";
        return;
    }

    /* 添加白边距，模拟小票纸张效果 */
    int margin = 16;
    int totalW = m_receiptImage.width() + margin * 2;
    int totalH = m_receiptImage.height() + margin * 2;

    QPixmap pixmap(totalW, totalH);
    pixmap.fill(QColor(255, 255, 255));

    QPainter painter(&pixmap);
    painter.drawImage(margin, margin, m_receiptImage);
    painter.end();

    /* 保存原始图像用于缩放 */
    m_receiptPixmap = pixmap;

    /* 设置初始样式 */
    ui->labelReceipt->setStyleSheet("");
    ui->labelReceipt->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    ui->labelReceipt->setContentsMargins(0, 0, 0, 0);

    /* 启用滚动 */
    ui->scrollAreaReceipt->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    /* 应用缩放 */
    scaleReceiptImage();

    /* 调整内容区域大小以容纳小票（高度取小票高度或可视高度中的较大值） */
    if (!m_receiptPixmap.isNull()) {
        int contentWidth = ui->scrollAreaReceipt->viewport()->width();
        int labelHeight = ui->labelReceipt->height();
        int contentHeight = qMax(labelHeight, ui->scrollAreaReceipt->viewport()->height());
        ui->scrollAreaReceiptContents->setFixedSize(contentWidth, contentHeight);
    }

    /* 自动滚到顶部 */
    if (ui->scrollAreaReceipt)
        ui->scrollAreaReceipt->verticalScrollBar()->setValue(0);
}

void MainWindow::scaleReceiptImage()
{
    if (m_receiptPixmap.isNull())
        return;

    if (ui->scrollAreaReceipt == nullptr)
        return;

    /* 获取滚动区域可视宽度 */
    int availableWidth = ui->scrollAreaReceipt->viewport()->width() - ui->scrollAreaReceipt->verticalScrollBar()->sizeHint().width();
    if (availableWidth <= 0)
        return;

    int originalWidth = m_receiptPixmap.width();
    if (originalWidth <= 0)
        return;

    /* 缩放到可用宽度，保持宽高比 */
    double scale = static_cast<double>(availableWidth) / originalWidth;
    int scaledHeight = static_cast<int>(m_receiptPixmap.height() * scale);

    /* 设置固定高度并缩放图片 */
    ui->labelReceipt->setFixedHeight(scaledHeight);
    ui->labelReceipt->setPixmap(m_receiptPixmap.scaled(
        availableWidth, scaledHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

QImage MainWindow::grayToQImage(const unsigned char *data, int width, int height)
{
    if (!data || width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; y++)
    {
        const unsigned char *srcLine = data + y * width;
        unsigned char *dstLine = image.scanLine(y);
        memcpy(dstLine, srcLine, width);
    }
    return image;
}

// ─── 状态面板刷新 ─────────────────────────────────────────────────────────────

/**
 * 从各 ATS API 读取当前状态，更新状态面板标签。
 * 每 500ms 由 m_statusTimer 触发一次。
 * 布尔值：绿色√ 表示正常，红色✕ 表示异常。
 */
void MainWindow::updateStatusPanel()
{
    // 防御性检查：确保 UI 完全就绪
    if (!ui || !ui->label_app_status)
        return;

    // 辅助：设置标签值，格式 <span style='color:#555555;'>名称：</span><span style='color:#色值;'>值</span>
    auto setKeyValRaw = [](QLabel *label, const QString &key, const QString &valColor, const QString &valText) {
        if (!label) return;
        label->setText(
            QString("<span style='color:#555555;'>%1</span>"
                    "<span style='color:%2;'>%3</span>")
                .arg(key).arg(valColor).arg(valText));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    };

    // ── 0: 程序状态 ──────────────────────────────────────────────────────────
    setKeyValRaw(ui->label_app_status, "程序状态：", m_appStarted ? "#4caf50" : "#ff9800",
        m_appStarted ? "运行中" : "等待启动");

    // ── 1: 网络状态 ──────────────────────────────────────────────────────────
    setKeyValRaw(ui->label_net_status, "网络状态：", ats_net_get_status() ? "#4caf50" : "#f44336",
        ats_net_get_status() ? "✓" : "✕");

    // ── 2: 网络类型 ──────────────────────────────────────────────────────────
    {
        QString modeText;
        switch (ats_net_get_mode()) {
            case ATS_NET_MODE_CELLUALR: modeText = "蜂窝"; break;
            case ATS_NET_MODE_WIFI:      modeText = "WiFi"; break;
            case ATS_NET_MODE_ETHERNET:  modeText = "以太网"; break;
            default:                      modeText = "未知"; break;
        }
        setKeyValRaw(ui->label_net_type, "网络类型：", "#555555", modeText);
    }

    // ── 3: WiFi模块状态 ──────────────────────────────────────────────────────
    setKeyValRaw(ui->label_wifi_module_status, "WiFi模块状态：", ats_net_wifi_get_module_status() ? "#4caf50" : "#f44336",
        ats_net_wifi_get_module_status() ? "✓" : "✕");

    // ── 4: WiFi热点名称 ──────────────────────────────────────────────────────
    {
        char *ssid = ats_net_wifi_get_ssid();
        QString ssidStr = (ssid && ssid[0] != '\0') ? QString::fromLatin1(ssid) : "(None)";
        setKeyValRaw(ui->label_wifi_ssid, "WiFi热点名称：", "#555555", ssidStr);
    }

    // ── 5: WiFi信号强度 ─────────────────────────────────────────────────────
    {
        int sig = ats_net_wifi_get_signal();
        setKeyValRaw(ui->label_wifi_signal, "WiFi信号强度：", "#555555", QString::number(sig));
    }

    // ── 6: 蜂窝MCC ──────────────────────────────────────────────────────────
    {
        int mcc = ats_net_cellular_get_mcc();
        setKeyValRaw(ui->label_cellular_mcc, "蜂窝MCC：", "#555555", QString::number(mcc));
    }

    // ── 7: 蜂窝MNC ──────────────────────────────────────────────────────────
    {
        int mnc = ats_net_cellular_get_mnc();
        setKeyValRaw(ui->label_cellular_mnc, "蜂窝MNC：", "#555555", QString::number(mnc));
    }

    // ── 8: 蜂窝LAC ──────────────────────────────────────────────────────────
    {
        int lac = ats_net_cellular_get_lac();
        setKeyValRaw(ui->label_cellular_lac, "蜂窝LAC：", "#555555", QString::number(lac));
    }

    // ── 9: 蜂窝CID ───────────────────────────────────────────────────────────
    {
        int cid = ats_net_cellular_get_cell_id();
        setKeyValRaw(ui->label_cellular_cid, "蜂窝CID：", "#555555", QString::number(cid));
    }

    // ── 10: 蜂窝信号强度 ─────────────────────────────────────────────────────
    {
        int sig = ats_net_cellular_get_signal();
        setKeyValRaw(ui->label_cellular_signal, "蜂窝信号强度：", "#555555", QString::number(sig));
    }

    // ── 11: 蜂窝IMSI ────────────────────────────────────────────────────────
    {
        char *imsi = ats_net_cellular_get_imsi();
        QString imsiStr = (imsi && imsi[0] != '\0') ? QString::fromLatin1(imsi) : "(None)";
        setKeyValRaw(ui->label_cellular_imsi, "蜂窝IMSI：", "#555555", imsiStr);
    }

    // ── 12: 蜂窝IMEI ─────────────────────────────────────────────────────────
    {
        char *imei = ats_net_cellular_get_imei();
        QString imeiStr = (imei && imei[0] != '\0') ? QString::fromLatin1(imei) : "(None)";
        setKeyValRaw(ui->label_cellular_imei, "蜂窝IMEI：", "#555555", imeiStr);
    }

    // ── 14: 序列号 ───────────────────────────────────────────────────────────
    {
        char *sn = ats_serial_number_get();
        QString snStr = (sn && sn[0] != '\0') ? QString::fromLatin1(sn) : "(None)";
        setKeyValRaw(ui->label_serial_number, "序列号：", "#555555", snStr);
    }

    // ── 15: 音频音量 ─────────────────────────────────────────────────────────────
    {
        size_t vol = 0;
        ats_audio_get_volume(&vol);
        setKeyValRaw(ui->label_volume, "音频音量：", "#555555", QString::number(vol));
    }

    // ── 16: 音频播放状态 ─────────────────────────────────────────────────────────
    setKeyValRaw(ui->label_audio_status, "音频播放状态：", ats_audio_is_playing() ? "#4caf50" : "#f44336",
        ats_audio_is_playing() ? "✓" : "✕");

    // ── 17: 音频队列数量 ──────────────────────────────────────────────────────
    setKeyValRaw(ui->label_audio_queue_count, "音频队列数量：", "#555555", QString::number(ats_audio_get_queue_count()));

    // ── 18: 网络链接数量 ─────────────────────────────────────────────────────────
    setKeyValRaw(ui->label_connection_count, "网络链接数量：", "#555555", QString::number(ats_net_get_connected_count()));

    // ── 19: 纸张状态 ─────────────────────────────────────────────────────────
    setKeyValRaw(ui->label_printer_paper_status, "纸张状态：", ats_printer_get_paper_status() ? "#4caf50" : "#f44336",
        ats_printer_get_paper_status() ? "✓" : "✕");
}

// ─── 屏幕区域渲染 ─────────────────────────────────────────────────────────────

/**
 * 在 labelScreen 上绘制设备屏幕内容。
 *
 * 逻辑：
 *   - 以 labelScreen 的可用高度为基准，按设备 4:3 比例计算显示宽度
 *   - 如果计算出的宽度超过可用宽度，则以宽度为基准缩放高度
 *   - 屏幕图像居中显示，两侧/上下留默认背景色
 *   - 未初始化或无帧数据时显示占位文字
 */
void MainWindow::updateScreenDisplay()
{
    QLabel *label = ui->labelScreen;
    int areaW = label->width();
    int areaH = label->height();
    if (areaW <= 0 || areaH <= 0)
        return;

    QPixmap pixmap(areaW, areaH);
    pixmap.fill(QColor("#1a1a2e"));

    const auto *fb = ats_lcd_get_framebuffer();
    unsigned short devW = ats_lcd_get_width();
    unsigned short devH = ats_lcd_get_height();

    if (fb && devW > 0 && devH > 0)
    {
        /* 按设备宽高比缩放，铺满整个 label 区域 */
        double scaleX = (double)areaW / devW;
        double scaleY = (double)areaH / devH;
        double scale  = qMin(scaleX, scaleY);

        int displayW = (int)(devW * scale);
        int displayH = (int)(devH * scale);

        /* 从 RGB565 帧缓冲构建 QImage */
        QImage img((const uchar *)fb, devW, devH, devW * 2, QImage::Format_RGB16);

        /* 居中绘制 */
        int offsetX = (areaW - displayW) / 2;
        int offsetY = (areaH - displayH) / 2;

        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            /* 绘制缩放后的屏幕内容 */
            painter.drawImage(QRect(offsetX, offsetY, displayW, displayH), img);
        }   /* painter 在此析构，与 pixmap 脱离绑定 */
    }
    else
    {
        /* 未初始化，显示占位文字 */
        {
            QPainter painter(&pixmap);
            painter.setPen(QColor("#ffffff"));
            painter.setFont(QFont("Consolas", 16));
            painter.drawText(pixmap.rect(), Qt::AlignCenter,
                             "[ No Signal ]");
        }   /* painter 在此析构，与 pixmap 脱离绑定 */
    }

    label->setPixmap(pixmap);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QMetaObject::invokeMethod(this, [this]() { updateScreenDisplay(); }, Qt::QueuedConnection);
}
