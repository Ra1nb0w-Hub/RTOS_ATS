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

private:
    Ui::ButtonsPanel *m_ui;
};
