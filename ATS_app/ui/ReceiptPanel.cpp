#include "ReceiptPanel.h"

ReceiptPanel::ReceiptPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ReceiptPanel)
{
    m_ui->setupUi(this);
}

ReceiptPanel::~ReceiptPanel()
{
    delete m_ui;
}
