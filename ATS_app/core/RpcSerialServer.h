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

static constexpr quint8 kRpcChannelCount = 4;

class RpcSerialServer : public QObject
{
    Q_OBJECT

public:
    explicit RpcSerialServer(QObject *parent = nullptr);
    ~RpcSerialServer() override;

    bool start(quint16 basePort);
    void stop();
    quint16 listenPort() const;
    bool isListening() const;
    RpcFrameProcessor *processor() const;

    void setElfPath(const QString &path);

signals:
    void logMessage(const QString &msg, const QString &level);
    void crashMessage(const QString &msg);

public slots:
    void writeFrame(const QByteArray &frame, quint8 channel);

private slots:
    void onNewConnection(int channel);
    void onSocketReadyRead(int channel);
    void onSocketDisconnected(int channel);

private:
    struct ChannelState
    {
        QTcpServer *server = nullptr;
        QTcpSocket *client = nullptr;
        QByteArray rxBuffer;
    };

    void closeClient(int channel);
    void processIncomingData(int channel);

    ChannelState m_channels[kRpcChannelCount];
    QString m_elfPath;
    quint16 m_basePort = 0;

    QThread *m_workerThread = nullptr;
    RpcFrameProcessor *m_processor = nullptr;
};
