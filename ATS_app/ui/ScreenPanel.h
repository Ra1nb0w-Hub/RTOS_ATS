#pragma once

#include <QWidget>
#include "ui_ScreenPanel.h"

class ScreenPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenPanel(QWidget *parent = nullptr);
    ~ScreenPanel();

    Ui::ScreenPanel *ui() const { return m_ui; }

private:
    Ui::ScreenPanel *m_ui;
};
