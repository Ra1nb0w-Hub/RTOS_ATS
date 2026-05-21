#pragma once

#include <QMainWindow>
#include <QVector>
#include <QMap>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QDomElement>
#include <QListWidgetItem>
class QAction;
class QCloseEvent;
#include "ScreenPanel.h"
#include "ButtonsPanel.h"
#include "TestCasesPanel.h"
#include "StatusPanel.h"
#include "LogPanel.h"
#include "ReceiptPanel.h"
#include "core/TestRunner.h"
#include "core/KeySimulator.h"
#include "core/AppThread.h"
#include "log/LogManager.h"
#include "qemu/QemuCortexMController.h"
#include "core/RpcSerialServer.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_audio.h"
#include "sdk/ats_net.h"
#include "sdk/ats_printer.h"

/**
 * @brief 测试用例配置结构
 */
struct TestCaseConfig {
    QString name;       // 测试用例名称（唯一标识，与 XML 中的 name 属性对应）
    QString script;     // 脚本路径（用于检查文件是否存在）
    bool enabled;       // 是否出现在测试列表中
    bool chosen;        // 是否默认勾选
    QMap<QString, QString> params;  // 额外参数（其他 XML 属性）
};

/**
 * @brief WiFi AP 信息结构
 */
struct WifiApInfo {
    QString ssid;
    QString mac;
    int signal;
};

/**
 * @brief 设备配置结构
 */
struct DeviceConfig {
    QString serialNumber;  // 设备序列号
    QString netMode;       // 网络模式: "cellular" 或 "wifi"
    QString netSsid;       // WiFi SSID (仅wifi模式有效)
    int wifiSignal;        // WiFi信号强度 (仅wifi模式有效)
    
    // 蜂窝网络相关配置
    int cellularMcc;       // 移动国家代码
    int cellularMnc;       // 移动网络代码
    int cellularLac;       // 位置区域码
    int cellularCid;       // 小区ID
    int cellularSignal;    // 蜂窝信号强度
    QString cellularImei;  // IMEI
    QString cellularImsi;  // IMSI
    
    // WiFi AP列表
    QVector<WifiApInfo> wifiApList;
    
    DeviceConfig()
        : wifiSignal(0),
          cellularMcc(0), cellularMnc(0), cellularLac(0), 
          cellularCid(0), cellularSignal(0) {}
    
    bool isValid() const {
        return !serialNumber.isEmpty();
    }
};

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief 主窗口
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 供外部（如 TestRunner）追加日志
    void appendLog(const QString &text, const QString &level = "INFO");

private slots:
    // ─── 测试控制 ────────────────────────────────
    void onRunSelected();
    void onRunAll();
    void onStop();

    // ─── 测试状态回调 ────────────────────────────
    void onTestStarted(const QString &caseName);
    void onTestFinished(const QString &caseName, bool passed, const QString &detail);
    void onAllTestsFinished(int total, int passed, int failed);
    void onProgressChanged(int current, int total);

    // ─── 日志操作 ────────────────────────────────
    void onSaveLog();
    void onClearLog();
    void onLogLevelChanged(const QString &level);
    void onLogFilterChanged(const QString &filter);

    // ─── 配置导入 ────────────────────────────────
    void onImportConfig();
    void onImportElf();

    // ─── 搜索测试用例 ────────────────────────────
    void onSearchCases(const QString &text);

    // ─── 测试用例列表点击 ─────────────────────────
    void onCaseItemClicked(QListWidgetItem *item);

    // ─── 小票预览 ────────────────────────────────
    void onShowReceipt();

    // ─── 设备物理按键 ────────────────────────────
    void onDeviceButtonPressed(int keyCode);
    void onDeviceButtonReleased(int keyCode);

private:
    void setupUi();
    void setupToolBar();
    void connectSignals();
    void updateTestCaseStatus(const QString &caseName, const QString &status);
    void setRunning(bool running);
    void startQemuWithImportedElf();
    void rebuildLogView();
    void updateScreenDisplay();
    void flushPendingLogs();
    void updateStatusPanel();
    void showReceiptPanel();

    // 配置导入相关
    bool loadConfigFromFile(const QString &filePath);
    void applyTestCaseConfig(const QVector<TestCaseConfig> &configs, const QString &xmlDir);
    void loadDefaultConfig();
    void applyDeviceConfig(const DeviceConfig &config);
    
    // 小票相关
    QImage grayToQImage(const unsigned char *data, int width, int height);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    Ui::MainWindow *ui;
    TestRunner     *m_runner;
    LogManager     *m_logManager;
    AppThread      *m_appThread;
    QemuCortexMController *m_qemuController;
    RpcSerialServer *m_logSerialServer;
    RpcSerialServer *m_rpcSerialServer;
    RpcSerialServer *m_lcdSerialServer;
    ScreenPanel    *m_screenPanel = nullptr;
    ButtonsPanel   *m_buttonsPanel = nullptr;
    TestCasesPanel *m_testCasesPanel = nullptr;
    StatusPanel    *m_statusPanel = nullptr;
    LogPanel       *m_logPanel = nullptr;
    ReceiptPanel   *m_receiptPanel = nullptr;
    QTimer         m_screenTimer;
    QTimer         m_logFlushTimer;
    QTimer         m_statusTimer;
    QStringList    m_pendingLogHtml;
    bool           m_logFlushScheduled = false;

    // 日志存储：所有日志始终保留，切换 level/filter 时重建显示
    struct LogEntry { QString html; QString level; QString text; };
    QVector<LogEntry> m_logEntries;

    // 小票区域数据
    QWidget      m_scrollAreaReceiptContents;
    QImage       m_receiptImage;
    QPixmap      m_receiptPixmap;  // 原始小票图像（带边距）
    QByteArray   m_receiptData;  // 小票灰度数据副本（避免异步回调时 buffer 被覆盖）
    int          m_receiptWidth = 0;
    int          m_receiptHeight = 0;

    // 小票图像缩放
    void scaleReceiptImage();

    // app 启动状态：false=等待电源键首次触发启动，true=已启动（电源键恢复正常功能）
    bool         m_appStarted = false;
    bool         m_closePending = false;
    QString      m_importedElfPath;
    QAction     *m_actionImportElf = nullptr;
    
    // 当前加载的配置文件路径
    QString      m_currentConfigPath;
    
    // 测试用例配置列表
    QVector<TestCaseConfig> m_testCaseConfigs;
    
    // 设备配置
    DeviceConfig m_deviceConfig;
};
