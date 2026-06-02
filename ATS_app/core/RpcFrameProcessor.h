#pragma once

#include <QObject>
#include <QString>

class RpcSerialServer;
class RpcNetWorker;
class QThread;

namespace RpcProtocol
{
struct Frame;
}

class RpcFrameProcessor : public QObject
{
public:
    explicit RpcFrameProcessor(RpcSerialServer *server);
    ~RpcFrameProcessor() override;

    void setElfPath(const QString &path);

    void dispatchFrame(const RpcProtocol::Frame &frame);

private slots:
    void onSockRecvFinished(quint8 service, quint8 command, int received, QByteArray data);
    void onSockConnectFinished(quint8 service, quint8 command, int ret);

private:
    void postResponse(const QByteArray &frame);
    void postLog(const QString &msg, const QString &level);
    void postCrash(const QString &msg);

    void sendResponse(quint8 service, quint8 command, const QByteArray &payload);
    void sendResponse(quint8 service, quint8 command);

    void handleCoreFrame(const RpcProtocol::Frame &frame);
    void handleLcdFrame(const RpcProtocol::Frame &frame);
    void handlePrinterFrame(const RpcProtocol::Frame &frame);
    void handleFsFrame(const RpcProtocol::Frame &frame);
    void handleNetFrame(const RpcProtocol::Frame &frame);
    void handleAudioFrame(const RpcProtocol::Frame &frame);
    void handleReaderFrame(const RpcProtocol::Frame &frame);

    QString m_elfPath;
    RpcSerialServer *m_server;

    QThread *m_netThread = nullptr;
    RpcNetWorker *m_netWorker = nullptr;
};
