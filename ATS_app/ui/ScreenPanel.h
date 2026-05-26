#pragma once

#include <QWidget>
#include <QTimer>
#include "ui_ScreenPanel.h"

class ScreenPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenPanel(QWidget *parent = nullptr);
    ~ScreenPanel();

    Ui::ScreenPanel *ui() const { return m_ui; }

    void startRendering();
    void updateScreenDisplay();

private:
    Ui::ScreenPanel *m_ui;
    QTimer m_screenTimer;
};
