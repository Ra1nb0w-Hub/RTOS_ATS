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
    enum class ChannelRole
    {
        Log,
        Rpc,
        Lcd
    };

    explicit RpcSerialServer(QObject *parent = nullptr);
    ~RpcSerialServer() override;

    void setChannelRole(ChannelRole role);
    ChannelRole channelRole() const;
    bool start(quint16 port);
    void stop();
    quint16 listenPort() const;
    bool isListening() const;

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
    QString roleName() const;

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_client = nullptr;
    QByteArray m_rxBuffer;
    ChannelRole m_role = ChannelRole::Rpc;
};
