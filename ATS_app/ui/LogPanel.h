#pragma once

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QTimer>
#include "ui_LogPanel.h"

class LogPanel : public QWidget
{
    Q_OBJECT

public:
    struct LogEntry {
        QString html;
        QString level;
        QString text;
    };

    explicit LogPanel(QWidget *parent = nullptr);
    ~LogPanel();

    Ui::LogPanel *ui() const { return m_ui; }

    void appendLog(const QString &text, const QString &level = "INFO");
    void clear();

private slots:
    void onSaveLog();
    void onClearLog();
    void onLogLevelChanged(const QString &level);
    void onLogFilterChanged(const QString &filter);

private:
    void flushPendingLogs();
    void rebuildLogView();
    void connectInternalSignals();

    Ui::LogPanel *m_ui;
    QVector<LogEntry> m_logEntries;
    QStringList m_pendingLogHtml;
    QTimer m_logFlushTimer;
    bool m_logFlushScheduled = false;
};
