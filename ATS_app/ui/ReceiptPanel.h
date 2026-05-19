#pragma once

#include <QWidget>
#include "ui_ReceiptPanel.h"

class ReceiptPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ReceiptPanel(QWidget *parent = nullptr);
    ~ReceiptPanel();

    Ui::ReceiptPanel *ui() const { return m_ui; }

private:
    Ui::ReceiptPanel *m_ui;
};
