#include "StatusPanel.h"

StatusPanel *StatusPanel::s_instance = nullptr;

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::StatusPanel)
{
    m_ui->setupUi(this);

    s_instance = this;

    ats_net_rpc_callback_t rpc_callback;
    rpc_callback.net_mode_change = StatusPanel::onNetModeChangeCallback;
    rpc_callback.net_status_change = StatusPanel::onNetStatusChangeCallback;
    rpc_callback.wifi_module_status_change = StatusPanel::onWifiModuleStatusChangeCallback;

    ats_net_rpc_register_callback(&rpc_callback);
}

StatusPanel::~StatusPanel()
{
    delete m_ui;
    s_instance = nullptr;
}

void StatusPanel::onNetModeChangeCallback(ats_net_mode_t mode)
{
    if (!s_instance) return;

    emit s_instance->netModeChanged(mode);
}

void StatusPanel::onNetStatusChangeCallback(bool status)
{
    if (!s_instance) return;

    emit s_instance->netStatusChanged(status);
}

void StatusPanel::onWifiModuleStatusChangeCallback(bool status)
{
    if (!s_instance) return;

    emit s_instance->wifiModuleStatusChanged(status);
}

void StatusPanel::setAppStarted(bool started)
{
    m_appStarted = started;
}

void StatusPanel::startMonitoring()
{
    m_statusTimer.setInterval(500);
    connect(&m_statusTimer, &QTimer::timeout, this, &StatusPanel::updateStatus);

    QTimer::singleShot(200, this, [this]() {
        m_statusTimer.start();
        updateStatus();
    });
}

void StatusPanel::updateStatus()
{
    QString labelContent;

    auto setKeyValRaw = [](QString &content, const QString &key, const QString &valColor, const QString &valText) {
        content.append(QString("<span style='color:#555555;'>%1</span>"
                    "<span style='color:%2;'>%3</span><br>")
                    .arg(key).arg(valColor).arg(valText));
    };

    QString appStatusText = m_appStarted ? "运行中" : "已停止";
    QString appStatusColor = m_appStarted ? "#4caf50" : "#f44336";
    setKeyValRaw(labelContent, "程序状态：", appStatusColor, appStatusText);

    {
        char *sn = ats_serial_number_get();
        QString snStr = (sn && sn[0] != '\0') ? QString::fromLatin1(sn) : "(None)";
        setKeyValRaw(labelContent, "序列号：", "#555555", snStr);
    }

    setKeyValRaw(labelContent, "网络状态：", ats_net_get_status() ? "#4caf50" : "#f44336", ats_net_get_status() ? "✓" : "✕");

    {
        size_t vol = 0;
        ats_audio_get_volume(&vol);
        setKeyValRaw(labelContent, "音频音量：", "#555555", QString::number(vol));
    }

    setKeyValRaw(labelContent, "音频播放状态：", ats_audio_is_playing() ? "#4caf50" : "#f44336", ats_audio_is_playing() ? "✓" : "✕");

    setKeyValRaw(labelContent, "音频队列数量：", "#555555", QString::number(ats_audio_get_queue_count()));

    setKeyValRaw(labelContent, "网络链接数量：", "#555555", QString::number(ats_net_get_connected_count()));

    setKeyValRaw(labelContent, "纸张状态：", ats_printer_get_paper_status() ? "#4caf50" : "#f44336", ats_printer_get_paper_status() ? "✓" : "✕");

    m_ui->labelStatus->setFixedHeight(m_ui->scrollAreaStatus->viewport()->height());
    m_ui->labelStatus->setText(labelContent);
    m_ui->labelStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
}
