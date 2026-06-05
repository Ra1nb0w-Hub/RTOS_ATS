#include "RpcSerialServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "RpcProtocol.h"
#include "../log/LogManager.h"

#include "RpcFrameProcessor.h"

RpcSerialServer::RpcSerialServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &RpcSerialServer::onNewConnection);

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

bool RpcSerialServer::start(quint16 port)
{
    if (m_server->isListening()) {
        return true;
    }

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        LogManager::logError(QString("RPC监听失败: %1").arg(m_server->errorString()));
        return false;
    }

    return true;
}

void RpcSerialServer::stop()
{
    closeClient();
    if (m_server->isListening()) {
        m_server->close();
    }
}

quint16 RpcSerialServer::listenPort() const
{
    return m_server->serverPort();
}

bool RpcSerialServer::isListening() const
{
    return m_server->isListening();
}

RpcFrameProcessor *RpcSerialServer::processor() const
{
    return m_processor;
}

void RpcSerialServer::writeFrame(const QByteArray &frame)
{
    if (m_client && m_client->isOpen()) {
        m_client->write(frame);
    }
}

void RpcSerialServer::setElfPath(const QString &path)
{
    m_elfPath = path;
    if (m_processor)
        m_processor->setElfPath(path);
}

void RpcSerialServer::onNewConnection()
{
    QTcpSocket *newClient = m_server->nextPendingConnection();
    if (!newClient) {
        return;
    }

    if (m_client) {
        newClient->disconnectFromHost();
        newClient->deleteLater();
        LogManager::logWarn("串口已有活动连接, 拒绝新的串口连接");
        return;
    }

    m_client = newClient;
    m_rxBuffer.clear();

    connect(m_client, &QTcpSocket::readyRead, this, &RpcSerialServer::onSocketReadyRead);
    connect(m_client, &QTcpSocket::disconnected, this, &RpcSerialServer::onSocketDisconnected);

    LogManager::logSys(QString("串口客户端已连接: %1:%2")
                           .arg(m_client->peerAddress().toString())
                           .arg(m_client->peerPort()));
}

void RpcSerialServer::onSocketReadyRead()
{
    if (!m_client) {
        return;
    }

    m_rxBuffer.append(m_client->readAll());
    processIncomingData();
}

void RpcSerialServer::onSocketDisconnected()
{
    LogManager::logSys("串口客户端已断开");
    closeClient();
}

void RpcSerialServer::closeClient()
{
    if (!m_client) {
        return;
    }

    m_client->deleteLater();
    m_client = nullptr;
    m_rxBuffer.clear();
}

void RpcSerialServer::processIncomingData()
{
    RpcProtocol::Frame frame;

    while (RpcProtocol::tryExtractFrame(&m_rxBuffer, &frame)) {
        QString elfPath = m_elfPath;
        QMetaObject::invokeMethod(m_processor, [this, frame, elfPath]() {
            m_processor->setElfPath(elfPath);
            m_processor->dispatchFrame(frame);
        }, Qt::QueuedConnection);
    }
}

void RpcSerialServer::onProcessorWriteResponse(QByteArray frame)
{
    if (m_client)
        m_client->write(frame);
}
