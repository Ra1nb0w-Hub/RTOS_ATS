#pragma once

#include <QObject>
#include <QString>

#include "sdk/ats_lcd.h"
#include "sdk/ats_printer.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_fs.h"
#include "sdk/ats_audio.h"
#include "sdk/ats_net.h"
#include "sdk/ats_reader.h"
#include "core/RpcProtocol.h"

class RpcSerialServer;
class RpcNetWorker;
class QThread;

class RpcFrameProcessor : public QObject
{
    Q_OBJECT

public:
    explicit RpcFrameProcessor(RpcSerialServer *server);
    ~RpcFrameProcessor() override;

    void setElfPath(const QString &path);
    void dispatchFrame(const RpcProtocol::Frame &frame, quint8 channel);

    void sendThreadInfoRequest();

public slots:
    void onReportPaperStatus(bool status);
    void onReportNetMode(ats_net_mode_t mode);
    void onReportNetStatus(bool status);
    void onReportWifiModuleStatus(bool status);

signals:
    void threadInfoReceived(const QVector<RpcProtocol::ThreadInfoEntry> &entries);

private slots:
    void onSockRecvFinished(quint8 service, quint8 command, int sock, int received, QByteArray data, quint8 requestId, quint8 channel);
    void onSockConnectFinished(quint8 service, quint8 command, int sock, int ret, quint8 requestId, quint8 channel);

private:
    void postResponse(const QByteArray &frame, quint8 channel);
    void postLog(const QString &msg, const QString &level);
    void postCrash(const QString &msg);

    void sendResponse(quint8 service, quint8 command, const QByteArray &payload);
    void sendResponse(quint8 service, quint8 command);
    void sendEvent(quint8 service, quint8 command, const QByteArray &payload);
    void sendEvent(quint8 service, quint8 command);

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
    quint8 m_currentRequestId = 0;
    quint8 m_currentChannel = 0;
    quint8 m_hostRequestId = 0;
};
