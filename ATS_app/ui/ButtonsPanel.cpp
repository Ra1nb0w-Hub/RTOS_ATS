#include "ButtonsPanel.h"

ButtonsPanel::ButtonsPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ButtonsPanel)
{
    m_ui->setupUi(this);
}

ButtonsPanel::~ButtonsPanel()
{
    delete m_ui;
}
