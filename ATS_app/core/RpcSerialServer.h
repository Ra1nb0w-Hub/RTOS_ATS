#pragma once

#include <QObject>

class QTcpServer;
class QTcpSocket;
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

    void setElfPath(const QString &path);

signals:
    void logMessage(const QString &msg, const QString &level);
    void crashMessage(const QString &msg);

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    void closeClient();
    void processIncomingData();
    void handleLogFrame(const RpcProtocol::Frame &frame);
    void handleRpcFrame(const RpcProtocol::Frame &frame);
    void handleLcdFrame(const RpcProtocol::Frame &frame);
    void handlePrinterFrame(const RpcProtocol::Frame &frame);
    void handleCoreFrame(const RpcProtocol::Frame &frame);

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QByteArray m_rxBuffer;
    QString m_elfPath;
};
