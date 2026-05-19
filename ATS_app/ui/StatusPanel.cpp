#include "StatusPanel.h"

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::StatusPanel)
{
    m_ui->setupUi(this);
}

StatusPanel::~StatusPanel()
{
    delete m_ui;
}
