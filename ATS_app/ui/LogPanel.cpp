#include "LogPanel.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QScrollBar>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPlainTextEdit>

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::LogPanel)
{
    m_ui->setupUi(this);
    connectInternalSignals();

    m_logFlushTimer.setSingleShot(true);
    m_logFlushTimer.setInterval(50);
    connect(&m_logFlushTimer, &QTimer::timeout, this, &LogPanel::flushPendingLogs);
}

LogPanel::~LogPanel()
{
    delete m_ui;
}

void LogPanel::connectInternalSignals()
{
    connect(m_ui->btnSaveLog, &QPushButton::clicked, this, &LogPanel::onSaveLog);
    connect(m_ui->btnClearLog, &QPushButton::clicked, this, &LogPanel::onClearLog);
    connect(m_ui->comboBoxLogLevel, &QComboBox::currentTextChanged,
            this, &LogPanel::onLogLevelChanged);
    connect(m_ui->lineEditLogFilter, &QLineEdit::textChanged,
            this, &LogPanel::onLogFilterChanged);
}

void LogPanel::appendLog(const QString &text, const QString &level)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    static const QMap<QString, QString> colorMap = {
        {"DEBUG", "#6a9fb5"},
        {"INFO",  "#c9d1d9"},
        {"WARN",  "#e6c07b"},
        {"ERROR", "#e06c75"},
        {"PASS",  "#98c379"},
        {"FAIL",  "#e06c75"},
        {"SYS",   "#61afef"},
    };

    QString color = colorMap.value(level.toUpper(), "#c9d1d9");
    QString escapedText = text.toHtmlEscaped().replace("\r\n", "<br>").replace("\n", "<br>");
    QString coloredText = QString("<span style='color:#666;'>[%1]</span> "
                                  "<span style='color:%2;font-weight:bold;'>[%3]</span> "
                                  "<span style='color:%2;'>%4</span>")
                              .arg(timestamp, color, level.toUpper(), escapedText);

    m_logEntries.append({coloredText, level.toUpper(), text});

    QString filterText = m_ui->lineEditLogFilter->text();
    if (!filterText.isEmpty() && !text.contains(filterText, Qt::CaseInsensitive))
        return;

    QString levelFilter = m_ui->comboBoxLogLevel->currentText();
    if (levelFilter != "ALL" && level.toUpper() != levelFilter)
        return;

    m_pendingLogHtml.append(coloredText);

    if (!m_logFlushScheduled) {
        m_logFlushScheduled = true;
        m_logFlushTimer.start();
    }
}

void LogPanel::flushPendingLogs()
{
    m_logFlushScheduled = false;
    if (m_pendingLogHtml.isEmpty())
        return;

    QString htmlBlock = m_pendingLogHtml.join("<br>");
    m_pendingLogHtml.clear();

    m_ui->plainTextEditLog->append(htmlBlock);

    if (m_ui->checkBoxAutoScroll->isChecked()) {
        auto *scrollbar = m_ui->plainTextEditLog->verticalScrollBar();
        if (scrollbar != nullptr)
            scrollbar->setValue(scrollbar->maximum());
    }
}

void LogPanel::rebuildLogView()
{
    m_ui->plainTextEditLog->clear();

    QString levelFilter = m_ui->comboBoxLogLevel->currentText();
    QString filterText  = m_ui->lineEditLogFilter->text();

    QStringList visibleEntries;
    for (const auto &entry : m_logEntries) {
        if (!filterText.isEmpty() && !entry.text.contains(filterText, Qt::CaseInsensitive))
            continue;
        if (levelFilter != "ALL" && entry.level != levelFilter)
            continue;

        visibleEntries.append(entry.html);
    }

    if (!visibleEntries.isEmpty()) {
        m_ui->plainTextEditLog->append(visibleEntries.join("<br>"));
    }

    if (m_ui->checkBoxAutoScroll->isChecked()) {
        auto *scrollbar = m_ui->plainTextEditLog->verticalScrollBar();
        if (scrollbar != nullptr)
            scrollbar->setValue(scrollbar->maximum());
    }
}

void LogPanel::clear()
{
    m_ui->plainTextEditLog->clear();
    m_logEntries.clear();
}

void LogPanel::onSaveLog()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Save Log", QString("ats_log_%1.txt")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&file);
        ts << m_ui->plainTextEditLog->toPlainText();
        appendLog(QString("Log saved to: %1").arg(path), "INFO");
    } else {
        appendLog(QString("Failed to save log: %1").arg(path), "ERROR");
    }
}

void LogPanel::onClearLog()
{
    clear();
}

void LogPanel::onLogLevelChanged(const QString & /*level*/)
{
    rebuildLogView();
}

void LogPanel::onLogFilterChanged(const QString & /*filter*/)
{
    rebuildLogView();
}
