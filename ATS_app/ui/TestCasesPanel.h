#pragma once

#include <QWidget>
#include "ui_TestCasesPanel.h"

class TestCasesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TestCasesPanel(QWidget *parent = nullptr);
    ~TestCasesPanel();

    Ui::TestCasesPanel *ui() const { return m_ui; }

private:
    Ui::TestCasesPanel *m_ui;
};
