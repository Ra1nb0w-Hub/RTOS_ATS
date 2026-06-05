#pragma once

#include <QWidget>
#include <QTimer>
#include "ui_StatusPanel.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_audio.h"
#include "sdk/ats_net.h"
#include "sdk/ats_printer.h"

class StatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);
    ~StatusPanel();

    Ui::StatusPanel *ui() const { return m_ui; }

    void setAppStarted(bool started);
    void startMonitoring();
    void updateStatus();

signals:
    void netModeChanged(ats_net_mode_t mode);
    void netStatusChanged(bool status);
    void wifiModuleStatusChanged(bool status);

private:
    static StatusPanel *s_instance;
    static void onNetModeChangeCallback(ats_net_mode_t mode);
    static void onNetStatusChangeCallback(bool status);
    static void onWifiModuleStatusChangeCallback(bool status);

    void (*wifi_module_status_change)(bool status);

    Ui::StatusPanel *m_ui;
    QTimer m_statusTimer;
    bool m_appStarted = false;
};
