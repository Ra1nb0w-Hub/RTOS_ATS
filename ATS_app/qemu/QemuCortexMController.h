#pragma once

#include <QObject>
#include <QString>

class QProcess;

class QemuCortexMController : public QObject
{
    Q_OBJECT

public:
    explicit QemuCortexMController(QObject *parent = nullptr);
    ~QemuCortexMController() override;

    void setFirmwarePath(const QString &firmwarePath);
    QString firmwarePath() const;
    bool hasFirmwarePath() const;

    bool start(quint16 logPort, quint16 rpcPort, quint16 lcdPort);
    void stop();
    bool isRunning() const;

signals:
    void logMessage(const QString &message, const QString &level);
    void started();
    void stopped();

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessErrorOccurred(int error);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    void emitLog(const QString &message, const QString &level);
    void processBufferedOutput(QByteArray *buffer, const QByteArray &chunk, const QString &level);
    void flushBufferedOutput(QByteArray *buffer, const QString &level);
    QString findQemuExecutablePath() const;

    QProcess *m_process = nullptr;
    QString m_firmwarePath;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    quint16 m_logPort = 0;
    quint16 m_rpcPort = 0;
    quint16 m_lcdPort = 0;
};
