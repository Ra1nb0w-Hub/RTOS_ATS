#pragma once

#include <QObject>

class QTcpServer;
class QTcpSocket;
class QThread;
class RpcFrameProcessor;
namespace RpcProtocol
{
struct Frame;
}

class RpcSerialServer : public QObject
{
    Q_OBJECT

public:
    explicit RpcSerialServer(QObject *parent = nullptr);
    ~RpcSerialServer() override;

    bool start(quint16 port);
    void stop();
    quint16 listenPort() const;
    bool isListening() const;
    RpcFrameProcessor *processor() const;

    void setElfPath(const QString &path);

signals:
    void logMessage(const QString &msg, const QString &level);
    void crashMessage(const QString &msg);

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onProcessorWriteResponse(QByteArray frame);

private:
    void closeClient();
    void processIncomingData();

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QByteArray m_rxBuffer;
    QString m_elfPath;

    QThread *m_workerThread = nullptr;
    RpcFrameProcessor *m_processor = nullptr;
};
