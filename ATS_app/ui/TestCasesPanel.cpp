#include "TestCasesPanel.h"

#include <QListWidgetItem>
#include <QPushButton>
#include <QLineEdit>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QDomNamedNodeMap>
#include <QMessageBox>
#include <QFont>
#include "sdk/ats_sys.h"
#include "sdk/ats_net.h"

TestCasesPanel::TestCasesPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::TestCasesPanel)
{
    m_ui->setupUi(this);
    connectInternalSignals();
}

TestCasesPanel::~TestCasesPanel()
{
    delete m_ui;
}

void TestCasesPanel::connectInternalSignals()
{
    connect(m_ui->lineEditSearch, &QLineEdit::textChanged,
            this, &TestCasesPanel::onSearchCases);
    connect(m_ui->listWidgetCases, &QListWidget::itemClicked,
            this, &TestCasesPanel::onCaseItemClicked);

    connect(m_ui->btnRunSelected, &QPushButton::clicked, this, [this]() {
        auto selected = collectSelectedScripts();
        emit runScriptsRequested(selected);
    });

    connect(m_ui->btnRunAll, &QPushButton::clicked, this, [this]() {
        auto all = collectAllScripts();
        emit runScriptsRequested(all);
    });

    connect(m_ui->btnStop, &QPushButton::clicked, this, &TestCasesPanel::stopRequested);
}

QMap<QString, QString> TestCasesPanel::collectSelectedScripts() const
{
    QMap<QString, QString> selectedMap;
    for (int i = 0; i < m_ui->listWidgetCases->count(); ++i) {
        auto *item = m_ui->listWidgetCases->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->data(Qt::UserRole).toString();
            QString script = item->data(Qt::UserRole + 1).toString();
            if (!script.isEmpty()) {
                selectedMap.insert(name, script);
            }
        }
    }
    return selectedMap;
}

QMap<QString, QString> TestCasesPanel::collectAllScripts() const
{
    QMap<QString, QString> allMap;
    for (int i = 0; i < m_ui->listWidgetCases->count(); ++i) {
        auto *item = m_ui->listWidgetCases->item(i);
        QString name = item->data(Qt::UserRole).toString();
        QString script = item->data(Qt::UserRole + 1).toString();
        if (!script.isEmpty()) {
            allMap.insert(name, script);
        }
    }
    return allMap;
}

void TestCasesPanel::updateStatus(const QString &caseName, const QString &status)
{
    for (int i = 0; i < m_ui->listWidgetCases->count(); ++i) {
        auto *item = m_ui->listWidgetCases->item(i);
        if (item->data(Qt::UserRole).toString() == caseName) {
            QString showName = item->data(Qt::UserRole).toString();
            QString displayText = QString("[%1]%2").arg(status).arg(showName);
            item->setText(displayText);
            QFont itemFont("Consolas", 10, QFont::DemiBold);
            item->setFont(itemFont);
            if (status == "PASS") {
                item->setForeground(QColor("#7dcea0"));
            } else if (status == "FAIL") {
                item->setForeground(QColor("#f1948a"));
            } else if (status == "Running") {
                item->setForeground(QColor("#ff8f40"));
            }
            break;
        }
    }
}

void TestCasesPanel::setRunning(bool running)
{
    m_ui->btnRunSelected->setEnabled(!running);
    m_ui->btnRunAll->setEnabled(!running);
    m_ui->btnStop->setEnabled(running);
    m_ui->listWidgetCases->setEnabled(!running);
    m_ui->btnImportConfig->setEnabled(!running);
}

void TestCasesPanel::setAppStarted(bool started)
{
    m_appStarted = started;
}

void TestCasesPanel::onSearchCases(const QString &text)
{
    for (int i = 0; i < m_ui->listWidgetCases->count(); ++i) {
        auto *item = m_ui->listWidgetCases->item(i);
        bool match = text.isEmpty() ||
                     item->data(Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void TestCasesPanel::onCaseItemClicked(QListWidgetItem *item)
{
    if (!item) return;

    Qt::CheckState newState = (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
    item->setCheckState(newState);

    QString scriptBaseName = item->data(Qt::UserRole).toString();
    for (auto &config : m_testCaseConfigs) {
        QString configScriptBaseName;
        if (!config.script.isEmpty()) {
            configScriptBaseName = QFileInfo(config.script).completeBaseName();
        } else {
            configScriptBaseName = config.name;
        }
        if (configScriptBaseName == scriptBaseName) {
            config.chosen = (newState == Qt::Checked);
            break;
        }
    }
}

// ─── 配置导入相关实现 ─────────────────────────────────────────────────────────

void TestCasesPanel::loadDefaultConfig()
{
    m_ui->listWidgetCases->clear();
    m_testCaseConfigs.clear();

    QString exePath = QCoreApplication::applicationDirPath();
    QString defaultPath = QDir(exePath).absoluteFilePath("scripts/config.xml");

    QFileInfo fileInfo(defaultPath);
    if (fileInfo.exists()) {
        emit logMessage("发现默认配置文件, 正在加载...", "SYS");
        if (loadConfigFromFile(defaultPath)) {
            m_currentConfigPath = defaultPath;
            emit logMessage("默认配置文件已加载完成", "SYS");
        }
    }
}

bool TestCasesPanel::loadConfigFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(QString("无法打开配置文件: %1").arg(filePath), "ERROR");
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
        emit logMessage(err, "ERROR");
        QMessageBox::warning(this, "导入失败", err);
        return false;
    }
    file.close();

    QDomElement root = doc.documentElement();
    if (root.tagName() != "config") {
        emit logMessage("配置文件格式错误: 根元素必须是 <config>", "ERROR");
        QMessageBox::warning(this, "导入失败", "配置文件格式错误: 根元素必须是 <config>");
        return false;
    }

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
        emit deviceConfigChanged(deviceConfig);
    }

    QDomElement casesEl = root.firstChildElement("cases");
    if (!casesEl.isNull()) {
        QVector<TestCaseConfig> newConfigs;
        QDomNodeList caseNodes = casesEl.elementsByTagName("case");

        for (int i = 0; i < caseNodes.count(); ++i) {
            QDomElement caseEl = caseNodes.at(i).toElement();
            if (caseEl.isNull()) continue;

            TestCaseConfig cfg;

            cfg.name = caseEl.attribute("name").trimmed();
            if (cfg.name.isEmpty()) {
                cfg.name = caseEl.firstChildElement("name").text().trimmed();
            }
            if (cfg.name.isEmpty()) continue;

            cfg.script = caseEl.attribute("script").trimmed();
            if (cfg.script.isEmpty()) {
                cfg.script = caseEl.firstChildElement("script").text().trimmed();
            }

            QString enabledStr = caseEl.attribute("enabled").trimmed().toLower();
            if (enabledStr.isEmpty()) {
                enabledStr = caseEl.firstChildElement("enabled").text().trimmed().toLower();
            }
            cfg.enabled = (enabledStr != "false" && enabledStr != "0");

            QString chosenStr = caseEl.attribute("chosen").trimmed().toLower();
            if (chosenStr.isEmpty()) {
                chosenStr = caseEl.firstChildElement("chosen").text().trimmed().toLower();
            }
            cfg.chosen = (chosenStr == "true" || chosenStr == "1");

            QDomNamedNodeMap attrs = caseEl.attributes();
            for (int j = 0; j < attrs.count(); ++j) {
                QDomAttr attr = attrs.item(j).toAttr();
                cfg.params.insert(attr.name(), attr.value());
            }

            newConfigs.append(cfg);
        }

        if (newConfigs.isEmpty()) {
            emit logMessage("配置文件中没有有效的测试用例", "WARN");
            QMessageBox::warning(this, "导入失败", "配置文件中没有有效的测试用例");
            return false;
        }

        QString xmlDir = QFileInfo(filePath).absolutePath();
        applyTestCaseConfig(newConfigs, xmlDir);
    }

    return true;
}

void TestCasesPanel::applyTestCaseConfig(const QVector<TestCaseConfig> &configs, const QString &xmlDir)
{
    emit logMessage(QString("发现 %1 个测试用例配置，正在导入中...").arg(configs.size()), "SYS");

    m_ui->listWidgetCases->clear();
    m_testCaseConfigs.clear();

    QFont itemFont("Consolas", 10);
    QColor itemForeground("#82aaff");

    for (const auto &config : configs) {
        if (!config.enabled) {
            emit logMessage(QString("测试用例已禁用，跳过[%1]").arg(config.name), "DEBUG");
            continue;
        }

        QString scriptFullPath;
        if (!config.script.isEmpty()) {
            QDir xmlDirectory(xmlDir);
            scriptFullPath = xmlDirectory.absoluteFilePath(config.script);

            if (!QFile::exists(scriptFullPath)) {
                emit logMessage(QString("测试用例脚本文件不存在，跳过[%1]")
                              .arg(config.name), "WARN");
                continue;
            }
        }

        auto *item = new QListWidgetItem(
            QString("%1").arg(config.name), m_ui->listWidgetCases);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(config.chosen ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, config.name);
        item->setData(Qt::UserRole + 1, scriptFullPath);
        item->setFont(itemFont);
        item->setForeground(itemForeground);

        TestCaseConfig savedConfig = config;
        savedConfig.script = scriptFullPath;
        m_testCaseConfigs.append(savedConfig);

        emit logMessage(QString("测试用例已加载: [%1]")
                      .arg(config.name), "INFO");
    }

    emit logMessage(QString("已成功导入 %1 个测试用例配置").arg(m_ui->listWidgetCases->count()), "SYS");
}

void TestCasesPanel::applyDeviceConfig(const DeviceConfig &config)
{
    m_deviceConfig = config;

    if (!config.serialNumber.isEmpty()) {
        QByteArray snBytes = config.serialNumber.toUtf8();
        ats_serial_number_set(snBytes.data());
        emit logMessage(QString("设备序列号已设置: %1").arg(config.serialNumber), "INFO");
    }

    if (!config.netMode.isEmpty()) {
        ats_net_mode_t mode = ATS_NET_MODE_CELLUALR;
        if (config.netMode == "wifi") {
            mode = ATS_NET_MODE_WIFI;
        } else if (config.netMode == "ethernet") {
            mode = ATS_NET_MODE_ETHERNET;
        } else if (config.netMode == "cellular") {
            mode = ATS_NET_MODE_CELLUALR;
        }
        ats_net_set_mode(mode);
        emit logMessage(QString("网络模式已设置: %1").arg(config.netMode), "INFO");
    }

    if (!config.netSsid.isEmpty()) {
        QByteArray ssidBytes = config.netSsid.toUtf8();
        ats_net_wifi_set_ssid(ssidBytes.data());
        emit logMessage(QString("WiFi SSID已设置: %1").arg(config.netSsid), "INFO");
    }

    ats_net_wifi_set_signal(config.wifiSignal);
    emit logMessage(QString("WiFi信号强度已设置: %1").arg(config.wifiSignal), "INFO");

    if (config.cellularMcc != 0) {
        ats_net_cellular_set_mcc(config.cellularMcc);
        emit logMessage(QString("蜂窝MCC已设置: %1").arg(config.cellularMcc), "INFO");
    }

    if (config.cellularMnc != 0) {
        ats_net_cellular_set_mnc(config.cellularMnc);
        emit logMessage(QString("蜂窝MNC已设置: %1").arg(config.cellularMnc), "INFO");
    }

    if (config.cellularLac != 0) {
        ats_net_cellular_set_lac(config.cellularLac);
        emit logMessage(QString("蜂窝LAC已设置: %1").arg(config.cellularLac), "INFO");
    }

    if (config.cellularCid != 0) {
        ats_net_cellular_set_cell_id(config.cellularCid);
        emit logMessage(QString("蜂窝CID已设置: %1").arg(config.cellularCid), "INFO");
    }

    if (config.cellularSignal != 0) {
        ats_net_cellular_set_signal(config.cellularSignal);
        emit logMessage(QString("蜂窝信号强度已设置: %1").arg(config.cellularSignal), "INFO");
    }

    if (!config.cellularImei.isEmpty()) {
        QByteArray imeiBytes = config.cellularImei.toUtf8();
        ats_net_cellular_set_imei(imeiBytes.data());
        emit logMessage(QString("蜂窝IMEI已设置: %1").arg(config.cellularImei), "INFO");
    }

    if (!config.cellularImsi.isEmpty()) {
        QByteArray imsiBytes = config.cellularImsi.toUtf8();
        ats_net_cellular_set_imsi(imsiBytes.data());
        emit logMessage(QString("蜂窝IMSI已设置: %1").arg(config.cellularImsi), "INFO");
    }

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
        emit logMessage(QString("WiFi AP列表已设置: %1个AP").arg(config.wifiApList.size()), "INFO");
    }
}
