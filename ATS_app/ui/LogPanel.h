#pragma once

#include <QWidget>
#include "ui_LogPanel.h"

class LogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);
    ~LogPanel();

    Ui::LogPanel *ui() const { return m_ui; }

private:
    Ui::LogPanel *m_ui;
};
