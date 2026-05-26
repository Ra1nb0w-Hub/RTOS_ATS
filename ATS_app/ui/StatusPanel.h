#pragma once

#include <QWidget>
#include <QTimer>
#include "ui_StatusPanel.h"

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

private:
    Ui::StatusPanel *m_ui;
    QTimer m_statusTimer;
    bool m_appStarted = false;
};
