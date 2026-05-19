#include "TestCasesPanel.h"

TestCasesPanel::TestCasesPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::TestCasesPanel)
{
    m_ui->setupUi(this);
}

TestCasesPanel::~TestCasesPanel()
{
    delete m_ui;
}
