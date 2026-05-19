#pragma once

#include <QWidget>
#include "ui_StatusPanel.h"

class StatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);
    ~StatusPanel();

    Ui::StatusPanel *ui() const { return m_ui; }

private:
    Ui::StatusPanel *m_ui;
};
