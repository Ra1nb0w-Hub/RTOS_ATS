#include "RpcNetWorker.h"

#include "../sdk/ats_net.h"

RpcNetWorker::RpcNetWorker(QObject *parent)
    : QObject(parent)
{
}

void RpcNetWorker::execSockRecv(int sock, unsigned int bufLen, unsigned int timeoutMs,
                                 quint8 service, quint8 command)
{
    QByteArray recvBuf(static_cast<int>(bufLen), '\0');
    int received = ats_sock_recv(sock, recvBuf.data(), bufLen, timeoutMs);

    QByteArray data;
    if (received > 0) {
        data = recvBuf.left(received);
    }

    emit sockRecvFinished(service, command, received, data);
}

void RpcNetWorker::execSockConnect(int sock, const QByteArray &host, quint16 port,
                                    unsigned int timeoutMs, quint8 service, quint8 command)
{
    int ret = ats_sock_connect(sock, host.constData(), port, timeoutMs);
    emit sockConnectFinished(service, command, ret);
}
