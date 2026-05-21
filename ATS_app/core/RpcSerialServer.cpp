#include "RpcSerialServer.h"

#include <QTcpServer>
#include <QTcpSocket>

#include "RpcProtocol.h"
#include "../log/LogManager.h"
#include "../sdk/ats_lcd.h"
#include "../sdk/ats_printer.h"

RpcSerialServer::RpcSerialServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &RpcSerialServer::onNewConnection);
}

RpcSerialServer::~RpcSerialServer()
{
    stop();
}

void RpcSerialServer::setChannelRole(ChannelRole role)
{
    m_role = role;
}

RpcSerialServer::ChannelRole RpcSerialServer::channelRole() const
{
    return m_role;
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

void RpcSerialServer::onNewConnection()
{
    QTcpSocket *newClient = m_server->nextPendingConnection();
    if (!newClient) {
        return;
    }

    if (m_client) {
        newClient->disconnectFromHost();
        newClient->deleteLater();
        LogManager::logWarn("RPC已有活动连接, 拒绝新的串口连接");
        return;
    }

    m_client = newClient;
    m_rxBuffer.clear();

    connect(m_client, &QTcpSocket::readyRead, this, &RpcSerialServer::onSocketReadyRead);
    connect(m_client, &QTcpSocket::disconnected, this, &RpcSerialServer::onSocketDisconnected);

    LogManager::logSys(QString("%1客户端已连接: %2:%3")
                           .arg(roleName())
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
    LogManager::logSys(QString("%1客户端已断开").arg(roleName()));
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
        switch (m_role) {
        case ChannelRole::Log:
            handleLogFrame(frame);
            break;
        case ChannelRole::Rpc:
            handleRpcFrame(frame);
            break;
        case ChannelRole::Lcd:
            handleLcdFrame(frame);
            break;
        }
    }
}

void RpcSerialServer::handleLogFrame(const RpcProtocol::Frame &frame)
{
    RpcProtocol::LogEvent event;
    if (RpcProtocol::decodeLogEvent(frame, &event)) {
        LogManager::log(event.level, event.message);
    }
}

void RpcSerialServer::handleRpcFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client) {
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandPing)) {
        m_client->write(RpcProtocol::buildResponseFrame(RpcProtocol::kServiceCore,
                                                        RpcProtocol::kCoreCommandPing,
                                                        frame.requestId));
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandCapabilities)) {
        QByteArray payload;
        payload.append(static_cast<char>(RpcProtocol::kServiceLog));
        payload.append(static_cast<char>(RpcProtocol::kServiceLcd));
        payload.append(static_cast<char>(RpcProtocol::kServicePrinter));
        m_client->write(RpcProtocol::buildResponseFrame(RpcProtocol::kServiceCore,
                                                        RpcProtocol::kCoreCommandCapabilities,
                                                        frame.requestId,
                                                        payload));
        return;
    }

    handlePrinterFrame(frame);
}

void RpcSerialServer::handleLcdFrame(const RpcProtocol::Frame &frame)
{
    RpcProtocol::LcdInitEvent lcdInit;
    if (RpcProtocol::decodeLcdInitEvent(frame, &lcdInit)) {
        if (ats_lcd_init(lcdInit.width, lcdInit.height) != 0) {
            LogManager::logWarn("LCD初始化事件处理失败");
        }
        return;
    }

    RpcProtocol::LcdRectEvent lcdRect;
    if (RpcProtocol::decodeLcdRectEvent(frame, RpcProtocol::kLcdCommandDrawRectangle, &lcdRect)) {
        if (ats_lcd_draw_rectangle(lcdRect.x, lcdRect.y, lcdRect.width, lcdRect.height, lcdRect.color) != 0) {
            LogManager::logWarn("LCD画矩形事件处理失败");
        }
        return;
    }

    if (RpcProtocol::decodeLcdRectEvent(frame, RpcProtocol::kLcdCommandFillRectangle, &lcdRect)) {
        if (ats_lcd_fill_rectangle(lcdRect.x, lcdRect.y, lcdRect.width, lcdRect.height, lcdRect.color) != 0) {
            LogManager::logWarn("LCD填充矩形事件处理失败");
        }
        return;
    }

    RpcProtocol::LcdBitmap1Event lcdBitmap1;
    if (RpcProtocol::decodeLcdBitmap1Event(frame, &lcdBitmap1)) {
        if (ats_lcd_draw_1bit_bitmap(lcdBitmap1.x,
                                     lcdBitmap1.y,
                                     lcdBitmap1.width,
                                     lcdBitmap1.height,
                                     reinterpret_cast<unsigned char *>(lcdBitmap1.bitmapData.data()),
                                     lcdBitmap1.foregroundColor,
                                     lcdBitmap1.backgroundColor,
                                     lcdBitmap1.isTransparent) != 0) {
            LogManager::logWarn("LCD 1bit 位图事件处理失败");
        }
        return;
    }

    RpcProtocol::LcdBitmap16Event lcdBitmap16;
    if (RpcProtocol::decodeLcdBitmap16Event(frame, &lcdBitmap16)) {
        if (ats_lcd_draw_16bit_bitmap(lcdBitmap16.x,
                                      lcdBitmap16.y,
                                      lcdBitmap16.width,
                                      lcdBitmap16.height,
                                      reinterpret_cast<unsigned short *>(lcdBitmap16.pixels.data())) != 0) {
            LogManager::logWarn("LCD 16bit 位图事件处理失败");
        }
        return;
    }

    if (RpcProtocol::isLcdDeinitEvent(frame)) {
        ats_lcd_deinit();
    }
}

void RpcSerialServer::handlePrinterFrame(const RpcProtocol::Frame &frame)
{
    quint8 enumValue = 0;

    if (!m_client) {
        return;
    }

    if (RpcProtocol::isPrinterEvent(frame, RpcProtocol::kPrinterCommandOpen)) {
        if (ats_printer_open() != 0) {
            LogManager::logWarn("Printer open event processing failed");
        }
        return;
    }

    if (RpcProtocol::isPrinterEvent(frame, RpcProtocol::kPrinterCommandClose)) {
        if (ats_printer_close() != 0) {
            LogManager::logWarn("Printer close event processing failed");
        }
        return;
    }

    if (RpcProtocol::decodePrinterEnumEvent(frame, RpcProtocol::kPrinterCommandSetAlign, &enumValue)) {
        if (ats_printer_set_align_mode(static_cast<ats_printer_align_mode_t>(enumValue)) != 0) {
            LogManager::logWarn("Printer align event processing failed");
        }
        return;
    }

    if (RpcProtocol::decodePrinterEnumEvent(frame, RpcProtocol::kPrinterCommandSetFontSize, &enumValue)) {
        if (ats_printer_set_font_size(static_cast<ats_printer_font_size_t>(enumValue)) != 0) {
            LogManager::logWarn("Printer font event processing failed");
        }
        return;
    }

    RpcProtocol::PrinterPrintTextEvent textEvent;
    if (RpcProtocol::decodePrinterPrintTextEvent(frame, &textEvent)) {
        QByteArray text = textEvent.text;
        text.append('\0');
        if (ats_printer_set_print_data(text.data(), textEvent.isEndOfLine) != 0) {
            LogManager::logWarn("Printer text event processing failed");
        }
        return;
    }

    RpcProtocol::PrinterPrintBitmapEvent bitmapEvent;
    if (RpcProtocol::decodePrinterPrintBitmapEvent(frame, &bitmapEvent)) {
        if (ats_printer_set_print_bitmap(reinterpret_cast<unsigned char *>(bitmapEvent.bitmapData.data()),
                                         bitmapEvent.width,
                                         bitmapEvent.height) != 0) {
            LogManager::logWarn("Printer bitmap event processing failed");
        }
        return;
    }

    if (RpcProtocol::decodePrinterEnumEvent(frame, RpcProtocol::kPrinterCommandSetPaperStatus, &enumValue)) {
        if (ats_printer_set_paper_status(enumValue != 0U) != 0) {
            LogManager::logWarn("Printer paper-status event processing failed");
        }
        return;
    }

    if (RpcProtocol::isPrinterPaperStatusRequest(frame)) {
        QByteArray payload;
        payload.append(ats_printer_get_paper_status() ? '\x01' : '\x00');
        m_client->write(RpcProtocol::buildResponseFrame(RpcProtocol::kServicePrinter,
                                                        RpcProtocol::kPrinterCommandGetPaperStatus,
                                                        frame.requestId,
                                                        payload));
    }
}

QString RpcSerialServer::roleName() const
{
    switch (m_role) {
    case ChannelRole::Log:
        return QStringLiteral("LOG UART");
    case ChannelRole::Rpc:
        return QStringLiteral("RPC UART");
    case ChannelRole::Lcd:
        return QStringLiteral("LCD UART");
    }

    return QStringLiteral("UART");
}
