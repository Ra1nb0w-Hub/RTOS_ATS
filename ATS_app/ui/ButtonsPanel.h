#pragma once

#include <QWidget>
#include "ui_ButtonsPanel.h"

class ButtonsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ButtonsPanel(QWidget *parent = nullptr);
    ~ButtonsPanel();

    Ui::ButtonsPanel *ui() const { return m_ui; }

    void setAppStarted(bool started);

signals:
    void startAppRequested();

private:
    void setupConnections();
    void onDeviceButtonPressed(int keyCode);
    void onDeviceButtonReleased(int keyCode);

    Ui::ButtonsPanel *m_ui;
    bool m_appStarted = false;
};
