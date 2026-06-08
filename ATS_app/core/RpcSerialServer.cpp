#include "RpcSerialServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "RpcProtocol.h"
#include "../log/LogManager.h"

#include "RpcFrameProcessor.h"

static const char *kChannelNames[] = {"CTRL", "DISP", "DATA", "LOG"};

RpcSerialServer::RpcSerialServer(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < kRpcChannelCount; ++i) {
        m_channels[i].server = new QTcpServer(this);
        connect(m_channels[i].server, &QTcpServer::newConnection,
                this, [this, i]() { onNewConnection(i); });
    }

    m_workerThread = new QThread(this);
    m_processor = new RpcFrameProcessor(this);
    m_processor->moveToThread(m_workerThread);

    m_workerThread->start();
}

RpcSerialServer::~RpcSerialServer()
{
    stop();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(5000);
    }
    delete m_processor;
    m_processor = nullptr;
}

bool RpcSerialServer::start(quint16 basePort)
{
    if (isListening()) {
        return true;
    }

    for (int i = 0; i < kRpcChannelCount; ++i) {
        const quint16 port = basePort + static_cast<quint16>(i);
        if (!m_channels[i].server->listen(QHostAddress::LocalHost, port)) {
            LogManager::logError(QString("RPC 通道 %1 监听失败(端口 %2): %3")
                                    .arg(kChannelNames[i])
                                    .arg(port)
                                    .arg(m_channels[i].server->errorString()));
            stop();
            return false;
        }
    }

    m_basePort = basePort;
    return true;
}

void RpcSerialServer::stop()
{
    for (int i = 0; i < kRpcChannelCount; ++i) {
        closeClient(i);
        if (m_channels[i].server->isListening()) {
            m_channels[i].server->close();
        }
    }
    m_basePort = 0;
}

quint16 RpcSerialServer::listenPort() const
{
    return m_basePort;
}

bool RpcSerialServer::isListening() const
{
    for (int i = 0; i < kRpcChannelCount; ++i) {
        if (m_channels[i].server->isListening()) {
            return true;
        }
    }
    return false;
}

RpcFrameProcessor *RpcSerialServer::processor() const
{
    return m_processor;
}

void RpcSerialServer::writeFrame(const QByteArray &frame, quint8 channel)
{
    if (channel >= kRpcChannelCount) {
        return;
    }

    QTcpSocket *client = m_channels[channel].client;
    if (client && client->isOpen()) {
        client->write(frame);
    }
}

void RpcSerialServer::setElfPath(const QString &path)
{
    m_elfPath = path;
    if (m_processor)
        m_processor->setElfPath(path);
}

void RpcSerialServer::onNewConnection(int channel)
{
    QTcpServer *server = m_channels[channel].server;
    QTcpSocket *newClient = server->nextPendingConnection();
    if (!newClient) {
        return;
    }

    if (m_channels[channel].client) {
        newClient->disconnectFromHost();
        newClient->deleteLater();
        LogManager::logWarn(QString("RPC 通道 %1 已有活动连接, 拒绝新连接").arg(kChannelNames[channel]));
        return;
    }

    m_channels[channel].client = newClient;
    m_channels[channel].rxBuffer.clear();

    connect(newClient, &QTcpSocket::readyRead,
            this, [this, channel]() { onSocketReadyRead(channel); });
    connect(newClient, &QTcpSocket::disconnected,
            this, [this, channel]() { onSocketDisconnected(channel); });

    LogManager::logSys(QString("RPC 通道 %1 客户端已连接: %2:%3")
                           .arg(kChannelNames[channel])
                           .arg(newClient->peerAddress().toString())
                           .arg(newClient->peerPort()));
}

void RpcSerialServer::onSocketReadyRead(int channel)
{
    QTcpSocket *client = m_channels[channel].client;
    if (!client) {
        return;
    }

    m_channels[channel].rxBuffer.append(client->readAll());
    processIncomingData(channel);
}

void RpcSerialServer::onSocketDisconnected(int channel)
{
    LogManager::logSys(QString("RPC 通道 %1 客户端已断开").arg(kChannelNames[channel]));
    closeClient(channel);
}

void RpcSerialServer::closeClient(int channel)
{
    QTcpSocket *client = m_channels[channel].client;
    if (!client) {
        return;
    }

    client->deleteLater();
    m_channels[channel].client = nullptr;
    m_channels[channel].rxBuffer.clear();
}

void RpcSerialServer::processIncomingData(int channel)
{
    RpcProtocol::Frame frame;

    while (RpcProtocol::tryExtractFrame(&m_channels[channel].rxBuffer, &frame)) {
        QString elfPath = m_elfPath;
        QMetaObject::invokeMethod(m_processor, [this, frame, elfPath, channel]() {
            m_processor->setElfPath(elfPath);
            m_processor->dispatchFrame(frame, static_cast<quint8>(channel));
        }, Qt::QueuedConnection);
    }
}
