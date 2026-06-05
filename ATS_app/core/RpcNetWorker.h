#pragma once

#include <QObject>

class RpcNetWorker : public QObject
{
    Q_OBJECT

public:
    explicit RpcNetWorker(QObject *parent = nullptr);

public slots:
    void execSockRecv(int sock, unsigned int bufLen, unsigned int timeoutMs,
                       quint8 service, quint8 command, quint8 requestId);
    void execSockConnect(int sock, const QByteArray &host, quint16 port,
                          unsigned int timeoutMs, quint8 service, quint8 command, quint8 requestId);

signals:
    void sockRecvFinished(quint8 service, quint8 command, int sock, int received, QByteArray data, quint8 requestId);
    void sockConnectFinished(quint8 service, quint8 command, int sock, int ret, quint8 requestId);
};
