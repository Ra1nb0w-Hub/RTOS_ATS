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

    connect(m_ui->btnDeviceFunc1, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_FUNC1);
    });
    connect(m_ui->btnDeviceFunc1, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceFunc2, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_FUNC2);
    });
    connect(m_ui->btnDeviceFunc2, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceFunc3, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_FUNC3);
    });
    connect(m_ui->btnDeviceFunc3, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceCancel, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_CANCEL);
    });
    connect(m_ui->btnDeviceCancel, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceClear, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_CLEAR);
    });
    connect(m_ui->btnDeviceClear, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceEnter, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_ENTER);
    });
    connect(m_ui->btnDeviceEnter, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceStar, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_STAR);
    });
    connect(m_ui->btnDeviceStar, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDevicePound, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_POUND);
    });
    connect(m_ui->btnDevicePound, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum0, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM0);
    });
    connect(m_ui->btnDeviceNum0, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum1, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM1);
    });
    connect(m_ui->btnDeviceNum1, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum2, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM2);
    });
    connect(m_ui->btnDeviceNum2, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum3, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM3);
    });
    connect(m_ui->btnDeviceNum3, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum4, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM4);
    });
    connect(m_ui->btnDeviceNum4, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum5, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM5);
    });
    connect(m_ui->btnDeviceNum5, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum6, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM6);
    });
    connect(m_ui->btnDeviceNum6, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum7, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM7);
    });
    connect(m_ui->btnDeviceNum7, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum8, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM8);
    });
    connect(m_ui->btnDeviceNum8, &QPushButton::released, this, [this]() {
        onDeviceButtonReleased(ATS_KEY_CODE_NONE);
    });

    connect(m_ui->btnDeviceNum9, &QPushButton::pressed, this, [this]() {
        onDeviceButtonPressed(ATS_KEY_CODE_NUM9);
    });
    connect(m_ui->btnDeviceNum9, &QPushButton::released, this, [this]() {
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
