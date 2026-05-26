#include "ButtonsPanel.h"
#include "sdk/ats_sys.h"

#include <QPushButton>

ButtonsPanel::ButtonsPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ButtonsPanel)
{
    m_ui->setupUi(this);
    setupConnections();
}

ButtonsPanel::~ButtonsPanel()
{
    delete m_ui;
}

void ButtonsPanel::setAppStarted(bool started)
{
    m_appStarted = started;
}

void ButtonsPanel::setupConnections()
{
    connect(m_ui->btnDevicePower, &QPushButton::pressed, this, [this]() {
        if (m_appStarted) {
            onDeviceButtonPressed(ATS_KEY_CODE_POWER);
        }
    });
    connect(m_ui->btnDevicePower, &QPushButton::released, this, [this]() {
        if (!m_appStarted) {
            emit startAppRequested();
        } else {
            onDeviceButtonReleased(ATS_KEY_CODE_NONE);
        }
    });

    connect(m_ui->btnDeviceMenu, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_MENU);
    });
    connect(m_ui->btnDeviceMenu, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceVolUp, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_VOLUME_INC);
    });
    connect(m_ui->btnDeviceVolUp, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceVolDown, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_VOLUME_DEC);
    });
    connect(m_ui->btnDeviceVolDown, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceReplay, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_REPLAY);
    });
    connect(m_ui->btnDeviceReplay, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });
}

void ButtonsPanel::onDeviceButtonPressed(int keyCode)
{
    ats_keypad_set_event(static_cast<uint8_t>(keyCode), true);
}

void ButtonsPanel::onDeviceButtonReleased(int keyCode)
{
    ats_keypad_set_event(static_cast<uint8_t>(keyCode), false);
}
