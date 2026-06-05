#include "RpcNetWorker.h"

#include "../sdk/ats_net.h"

RpcNetWorker::RpcNetWorker(QObject *parent)
    : QObject(parent)
{
}

void RpcNetWorker::execSockRecv(int sock, unsigned int bufLen, unsigned int timeoutMs,
                                 quint8 service, quint8 command, quint8 requestId)
{
    QByteArray recvBuf(static_cast<int>(bufLen), '\0');
    int received = ats_sock_recv(sock, recvBuf.data(), bufLen, timeoutMs);

    QByteArray data;
    if (received > 0) {
        data = recvBuf.left(received);
    }

    emit sockRecvFinished(service, command, sock, received, data, requestId);
}

void RpcNetWorker::execSockConnect(int sock, const QByteArray &host, quint16 port,
                                    unsigned int timeoutMs, quint8 service, quint8 command, quint8 requestId)
{
    int ret = ats_sock_connect(sock, host.constData(), port, timeoutMs);
    emit sockConnectFinished(service, command, sock, ret, requestId);
}
