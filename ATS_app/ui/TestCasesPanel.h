#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QDomElement>
#include "ui_TestCasesPanel.h"

class QListWidgetItem;

struct TestCaseConfig {
    QString name;
    QString script;
    bool enabled;
    bool chosen;
    QMap<QString, QString> params;
};

struct WifiApInfo {
    QString ssid;
    QString mac;
    int signal;
};

struct DeviceConfig {
    QString serialNumber;
    QString netMode;
    QString netSsid;
    int wifiSignal;

    int cellularMcc;
    int cellularMnc;
    int cellularLac;
    int cellularCid;
    int cellularSignal;
    QString cellularImei;
    QString cellularImsi;

    QVector<WifiApInfo> wifiApList;

    DeviceConfig()
        : wifiSignal(0),
          cellularMcc(0), cellularMnc(0), cellularLac(0),
          cellularCid(0), cellularSignal(0) {}

    bool isValid() const {
        return !serialNumber.isEmpty();
    }
};

class TestCasesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TestCasesPanel(QWidget *parent = nullptr);
    ~TestCasesPanel();

    Ui::TestCasesPanel *ui() const { return m_ui; }

    void loadDefaultConfig();
    bool loadConfigFromFile(const QString &filePath);

    QMap<QString, QString> collectSelectedScripts() const;
    QMap<QString, QString> collectAllScripts() const;

    void updateStatus(const QString &caseName, const QString &status);
    void setRunning(bool running);
    void setAppStarted(bool started);
    bool isAppStarted() const { return m_appStarted; }

    const DeviceConfig &deviceConfig() const { return m_deviceConfig; }
    const QVector<TestCaseConfig> &testCaseConfigs() const { return m_testCaseConfigs; }

signals:
    void logMessage(const QString &text, const QString &level);
    void runScriptsRequested(const QMap<QString, QString> &scripts);
    void stopRequested();
    void deviceConfigChanged(const DeviceConfig &config);

private slots:
    void onSearchCases(const QString &text);
    void onCaseItemClicked(QListWidgetItem *item);

private:
    void applyTestCaseConfig(const QVector<TestCaseConfig> &configs, const QString &xmlDir);
    void applyDeviceConfig(const DeviceConfig &config);
    void connectInternalSignals();

    Ui::TestCasesPanel *m_ui;
    QVector<TestCaseConfig> m_testCaseConfigs;
    DeviceConfig m_deviceConfig;
    QString m_currentConfigPath;
    bool m_appStarted = false;
};
