#include "LogPanel.h"

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::LogPanel)
{
    m_ui->setupUi(this);
}

LogPanel::~LogPanel()
{
    delete m_ui;
}
