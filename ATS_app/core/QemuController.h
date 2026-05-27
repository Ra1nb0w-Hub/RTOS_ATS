#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTcpSocket;

class QemuController : public QObject
{
    Q_OBJECT

public:
    explicit QemuController(QObject *parent = nullptr);
    ~QemuController() override;

    void setFirmwarePath(const QString &firmwarePath);
    QString firmwarePath() const;
    bool hasFirmwarePath() const;

    bool start(quint16 serialPort);
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
    void onQmpConnected();
    void onQmpDisconnected();
    void onQmpReadyRead();

private:
    void emitLog(const QString &message, const QString &level);
    void processBufferedOutput(QByteArray *buffer, const QByteArray &chunk, const QString &level);
    void flushBufferedOutput(QByteArray *buffer, const QString &level);
    QString findQemuExecutablePath() const;
    quint16 findFreePort() const;
    void connectToQmp();
    void handleQmpMessage(const QByteArray &message);
    void sendQmpCommand(const QByteArray &command);
    void shutdownViaQmp();

    QProcess *m_process = nullptr;
    QTcpSocket *m_qmpSocket = nullptr;
    QString m_firmwarePath;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QByteArray m_qmpBuffer;
    quint16 m_serialPort = 0;
    quint16 m_qmpPort = 0;
    bool m_qmpNegotiated = false;
    bool m_shuttingDown = false;
};
