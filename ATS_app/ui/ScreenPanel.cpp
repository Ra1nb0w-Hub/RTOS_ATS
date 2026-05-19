#include "ScreenPanel.h"

ScreenPanel::ScreenPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ScreenPanel)
{
    m_ui->setupUi(this);
}

ScreenPanel::~ScreenPanel()
{
    delete m_ui;
}
