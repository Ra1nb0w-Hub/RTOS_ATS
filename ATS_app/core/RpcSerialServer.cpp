#include "RpcSerialServer.h"

#include <QTcpServer>
#include <QTcpSocket>

#include "RpcProtocol.h"
#include "Addr2LineResolver.h"
#include "../log/LogManager.h"
#include "../sdk/ats_lcd.h"
#include "../sdk/ats_printer.h"
#include "../sdk/ats_sys.h"
#include "../sdk/ats_fs.h"
#include "../sdk/ats_audio.h"
#include "../sdk/ats_net.h"
#include "../sdk/ats_reader.h"

static qint32 readInt32Le(const char *data, int offset)
{
    return static_cast<qint32>(
        static_cast<quint8>(data[offset])
        | (static_cast<quint32>(static_cast<quint8>(data[offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<quint8>(data[offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(data[offset + 3])) << 24));
}

static QByteArray buildInt32Response(qint32 value)
{
    QByteArray payload(4, '\0');
    payload[0] = static_cast<char>(value & 0xFF);
    payload[1] = static_cast<char>((value >> 8) & 0xFF);
    payload[2] = static_cast<char>((value >> 16) & 0xFF);
    payload[3] = static_cast<char>((value >> 24) & 0xFF);
    return payload;
}

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

void RpcSerialServer::setElfPath(const QString &path)
{
    m_elfPath = path;
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
        switch (frame.service) {
        case RpcProtocol::kServiceCore:
            handleCoreFrame(frame);
            break;
        case RpcProtocol::kServiceLcd:
            handleLcdFrame(frame);
            break;
        case RpcProtocol::kServicePrinter:
            handleRpcFrame(frame);
            break;
        case RpcProtocol::kServiceFs:
            handleFsFrame(frame);
            break;
        case RpcProtocol::kServiceNet:
            handleNetFrame(frame);
            break;
        case RpcProtocol::kServiceAudio:
            handleAudioFrame(frame);
            break;
        case RpcProtocol::kServiceReader:
            handleReaderFrame(frame);
            break;
        default:
            break;
        }
    }
}

void RpcSerialServer::handleLogFrame(const RpcProtocol::Frame &frame)
{
    RpcProtocol::LogEvent event;
    if (RpcProtocol::decodeLogEvent(frame, &event)) {
        emit logMessage(event.message, "INFO");
    }
}

void RpcSerialServer::handleCoreFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType == RpcProtocol::kFrameTypeEvent) {
        if (frame.command == RpcProtocol::kCoreCommandCrash) {
            RpcProtocol::CrashEvent crash;
            if (RpcProtocol::decodeCrashEvent(frame, &crash) && !crash.addresses.isEmpty()) {
                QString msg = QString("Fireware HardFault:");

                if (!m_elfPath.isEmpty()) {
                    Addr2LineResolver resolver(m_elfPath);
                    if (resolver.isValid()) {
                        for (int i = 0; i < crash.addresses.size(); ++i) {
                            const QString location = resolver.resolve(crash.addresses[i]);
                            if (!location.isEmpty()) {
                                if (i == 0) {
                                    msg += QString("\n[PC] %1").arg(location);
                                } else {
                                    msg += QString("\n[LR] %1").arg(location);
                                }
                            }
                        }
                    } else {
                        msg += QString("\n  (addr2line 工具或 ELF 文件不可用，无法解析位置)");
                    }
                }

                emit crashMessage(msg);
            }
            return;
        }

        if (frame.command == RpcProtocol::kCoreCommandWriteLog)
        {
            handleLogFrame(frame);
            return;
        }
    }

    if (frame.frameType != RpcProtocol::kFrameTypeRequest || !m_client) {
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandGetDateTime)) {
        ats_datetime_t dt;
        int ret = ats_datetime_get(&dt);
        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetDateTime,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(dt.uiYear & 0xFF));
        respPayload.append(static_cast<char>((dt.uiYear >> 8) & 0xFF));
        respPayload.append(static_cast<char>(dt.uiMonth));
        respPayload.append(static_cast<char>(dt.uiDay));
        respPayload.append(static_cast<char>(dt.uiHour));
        respPayload.append(static_cast<char>(dt.uiMinute));
        respPayload.append(static_cast<char>(dt.uiSecond));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetDateTime, respPayload));
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandSetDateTime)) {
        RpcProtocol::DateTime dt;
        if (RpcProtocol::decodeCoreDateTimeRequest(frame, &dt)) {
            ats_datetime_t atsDt;
            atsDt.uiYear   = dt.year;
            atsDt.uiMonth  = dt.month;
            atsDt.uiDay    = dt.day;
            atsDt.uiHour   = dt.hour;
            atsDt.uiMinute = dt.minute;
            atsDt.uiSecond = dt.second;
            int ret = ats_datetime_set(&atsDt);
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandSetDateTime,
                buildInt32Response(static_cast<qint32>(ret))));
        }
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandGetTimestamp)) {
        unsigned long ts = ats_timestamp_get();
        quint32 val = static_cast<quint32>(ts);
        QByteArray payload(4, '\0');
        payload[0] = static_cast<char>(val & 0xFF);
        payload[1] = static_cast<char>((val >> 8) & 0xFF);
        payload[2] = static_cast<char>((val >> 16) & 0xFF);
        payload[3] = static_cast<char>((val >> 24) & 0xFF);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetTimestamp, payload));
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandGetSerialNumber)) {
        char *sn = ats_serial_number_get();
        QByteArray payload;
        if (sn != nullptr && sn[0] != '\0') {
            payload = QByteArray(sn, static_cast<int>(strlen(sn)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetSerialNumber, payload));
        return;
    }
}

void RpcSerialServer::handleRpcFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client) {
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
                                                        payload));
    }
}

void RpcSerialServer::handleFsFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client || frame.frameType != RpcProtocol::kFrameTypeRequest) {
        return;
    }

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandOpen)) {
        if (p.size() < 3)
            return;

        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 3 + pathLen)
            return;

        const QByteArray path = p.mid(1, pathLen);
        const int flags = static_cast<int>(data[1 + pathLen]);
        const int mode = static_cast<int>(data[2 + pathLen]);

        ats_fs_handle_t handle;
        int ret = ats_fs_open(&handle, path.constData(),
                              static_cast<ats_fs_open_flags_t>(flags),
                              static_cast<ats_fs_open_mode_t>(mode));
        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceFs, RpcProtocol::kFsCommandOpen,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        qint32 handleVal = static_cast<qint32>(handle);
        respPayload.append(static_cast<char>(handleVal & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 8) & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 16) & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 24) & 0xFF));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandOpen, respPayload));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandClose)) {
        if (p.size() < 4)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        int ret = ats_fs_close(handle);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandClose,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandRead)) {
        if (p.size() < 8)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        const quint32 count = static_cast<quint32>(readInt32Le(data, 4));

        QByteArray readBuf(static_cast<int>(count), '\0');
        int bytesRead = ats_fs_read(handle, readBuf.data(), static_cast<size_t>(count));

        QByteArray respPayload(4, '\0');
        respPayload[0] = static_cast<char>(bytesRead & 0xFF);
        respPayload[1] = static_cast<char>((bytesRead >> 8) & 0xFF);
        respPayload[2] = static_cast<char>((bytesRead >> 16) & 0xFF);
        respPayload[3] = static_cast<char>((bytesRead >> 24) & 0xFF);

        if (bytesRead > 0) {
            respPayload.append(readBuf.left(bytesRead));
        }

        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandRead, respPayload));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandWrite)) {
        if (p.size() < 4)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        const QByteArray writeData = p.mid(4);

        int bytesWritten = ats_fs_write(handle, writeData.constData(),
                                        static_cast<size_t>(writeData.size()));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandWrite,
            buildInt32Response(static_cast<qint32>(bytesWritten))));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandSeek)) {
        if (p.size() < 9)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        const size_t offset = static_cast<size_t>(readInt32Le(data, 4));
        const int whence = static_cast<int>(data[8]);

        int ret = ats_fs_seek(handle, offset, static_cast<ats_fs_whence_t>(whence));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandSeek,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandSize)) {
        if (p.size() < 2)
            return;

        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + pathLen)
            return;

        const QByteArray path = p.mid(1, pathLen);
        int ret = ats_fs_size(path.constData());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandSize,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandRemove)) {
        if (p.size() < 2)
            return;

        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + pathLen)
            return;

        const QByteArray path = p.mid(1, pathLen);
        int ret = ats_fs_remove(path.constData());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandRemove,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandExist)) {
        if (p.size() < 2)
            return;

        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + pathLen)
            return;

        const QByteArray path = p.mid(1, pathLen);
        int ret = ats_fs_exist(path.constData());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceFs, RpcProtocol::kFsCommandExist,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }
}

void RpcSerialServer::handleNetFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client || frame.frameType != RpcProtocol::kFrameTypeRequest) {
        return;
    }

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockCreate)) {
        if (p.size() < 3) return;
        const int family = static_cast<int>(data[0]);
        const int type = static_cast<int>(data[1]);
        const int protocol = static_cast<int>(data[2]);
        ats_sock_t sock;
        int ret = ats_sock_create(&sock,
                                   static_cast<ats_sock_family_t>(family),
                                   static_cast<ats_sock_type_t>(type),
                                   static_cast<ats_sock_protocol_t>(protocol));
        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockCreate,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }
        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        qint32 sockVal = static_cast<qint32>(sock);
        respPayload.append(static_cast<char>(sockVal & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 8) & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 16) & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 24) & 0xFF));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockCreate, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockConnect)) {
        if (p.size() < 8) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        const quint8 hostLen = static_cast<quint8>(data[4]);
        if (p.size() < 5 + hostLen + 6) return;
        const QByteArray host = p.mid(5, hostLen);
        const quint16 port = static_cast<quint16>(
            static_cast<quint8>(data[5 + hostLen]) |
            (static_cast<quint32>(static_cast<quint8>(data[5 + hostLen + 1])) << 8));
        const unsigned int timeoutMs = static_cast<unsigned int>(
            readInt32Le(data, 5 + hostLen + 2));
        int ret = ats_sock_connect(sock, host.constData(), port, timeoutMs);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockConnect,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockSend)) {
        if (p.size() < 4) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        const QByteArray sendData = p.mid(4);
        int sent = ats_sock_send(sock, sendData.constData(),
                                 static_cast<unsigned int>(sendData.size()));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockSend,
            buildInt32Response(static_cast<qint32>(sent))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockRecv)) {
        if (p.size() < 12) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        const unsigned int bufLen = static_cast<unsigned int>(readInt32Le(data, 4));
        const unsigned int timeoutMs = static_cast<unsigned int>(readInt32Le(data, 8));

        QByteArray recvBuf(static_cast<int>(bufLen), '\0');
        int received = ats_sock_recv(sock, recvBuf.data(), bufLen, timeoutMs);

        QByteArray respPayload(4, '\0');
        respPayload[0] = static_cast<char>(received & 0xFF);
        respPayload[1] = static_cast<char>((received >> 8) & 0xFF);
        respPayload[2] = static_cast<char>((received >> 16) & 0xFF);
        respPayload[3] = static_cast<char>((received >> 24) & 0xFF);
        if (received > 0) {
            respPayload.append(recvBuf.left(received));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockRecv, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockClose)) {
        if (p.size() < 4) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        int ret = ats_sock_close(sock);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockClose,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSetMode)) {
        if (p.size() < 1) return;
        int ret = ats_net_set_mode(static_cast<ats_net_mode_t>(data[0]));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSetMode,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandGetMode)) {
        qint32 mode = static_cast<qint32>(ats_net_get_mode());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandGetMode,
            buildInt32Response(mode)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSetStatus)) {
        if (p.size() < 1) return;
        int ret = ats_net_set_status(data[0] != 0);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSetStatus,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandGetStatus)) {
        bool status = ats_net_get_status();
        QByteArray respPayload;
        respPayload.append(status ? '\x01' : '\x00');
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandGetStatus, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiSetModuleStatus)) {
        if (p.size() < 1) return;
        int ret = ats_net_wifi_set_module_status(data[0] != 0);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiSetModuleStatus,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetModuleStatus)) {
        bool status = ats_net_wifi_get_module_status();
        QByteArray respPayload;
        respPayload.append(status ? '\x01' : '\x00');
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetModuleStatus, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiSetSsid)) {
        if (p.size() < 2) return;
        const quint8 ssidLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + ssidLen) return;
        const QByteArray ssid = p.mid(1, ssidLen);
        int ret = ats_net_wifi_set_ssid(ssid.constData());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiSetSsid,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetSsid)) {
        char *ssid = ats_net_wifi_get_ssid();
        QByteArray respPayload;
        if (ssid) {
            respPayload = QByteArray(ssid, static_cast<int>(strlen(ssid)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetSsid, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiSetSignal)) {
        if (p.size() < 4) return;
        int ret = ats_net_wifi_set_signal(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiSetSignal,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetSignal)) {
        int signal = ats_net_wifi_get_signal();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetSignal,
            buildInt32Response(static_cast<qint32>(signal))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiSetApList)) {
        if (p.size() < 2) return;
        const quint16 count = static_cast<quint16>(
            static_cast<quint8>(data[0]) | (static_cast<quint32>(static_cast<quint8>(data[1])) << 8));
        const int expectedSize = 2 + static_cast<int>(count) * 92;
        if (count > 0 && p.size() < expectedSize) return;

        QVector<ats_net_wifi_ap_t> apList;
        for (quint16 i = 0; i < count; ++i) {
            ats_net_wifi_ap_t ap;
            memset(&ap, 0, sizeof(ap));
            const int off = 2 + static_cast<int>(i) * 92;
            memcpy(ap.ssid, data + off, 64);
            ap.ssid[63] = '\0';
            ap.rssi = readInt32Le(data, off + 64);
            memcpy(ap.mac, data + off + 68, 24);
            ap.mac[23] = '\0';
            apList.append(ap);
        }
        int ret = ats_net_wifi_set_ap_list(apList.data(),
                                            static_cast<unsigned int>(count));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiSetApList,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetApList)) {
        ats_net_wifi_ap_t *apList = nullptr;
        unsigned int count = 0;
        int ret = ats_net_wifi_get_ap_list(&apList, &count);
        if (ret != 0)
        {
            if (apList)
                free(apList);

            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetApList,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }
        else
        {
            QByteArray respPayload;
            QByteArray respRet = buildInt32Response(static_cast<qint32>(ret));

            respPayload.append(respRet);
            respPayload.append(static_cast<char>(count & 0xFF));
            respPayload.append(static_cast<char>((count >> 8) & 0xFF));

            for (unsigned int i = 0; i < count; ++i) {
                QByteArray ssid(apList[i].ssid, 64);
                respPayload.append(ssid);
                QByteArray rssi = buildInt32Response(apList[i].rssi);
                respPayload.append(rssi);
                QByteArray mac(apList[i].mac, 24);
                respPayload.append(mac);
            }

            if (apList)
                free(apList);

            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetApList, respPayload));
            return;
        }
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetMcc)) {
        if (p.size() < 4) return;
        int ret = ats_net_cellular_set_mcc(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetMcc,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetMcc)) {
        int val = ats_net_cellular_get_mcc();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetMcc,
            buildInt32Response(static_cast<qint32>(val))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetMnc)) {
        if (p.size() < 4) return;
        int ret = ats_net_cellular_set_mnc(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetMnc,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetMnc)) {
        int val = ats_net_cellular_get_mnc();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetMnc,
            buildInt32Response(static_cast<qint32>(val))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetLac)) {
        if (p.size() < 4) return;
        int ret = ats_net_cellular_set_lac(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetLac,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetLac)) {
        int val = ats_net_cellular_get_lac();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetLac,
            buildInt32Response(static_cast<qint32>(val))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetCellId)) {
        if (p.size() < 4) return;
        int ret = ats_net_cellular_set_cell_id(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetCellId,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetCellId)) {
        int val = ats_net_cellular_get_cell_id();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetCellId,
            buildInt32Response(static_cast<qint32>(val))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetSignal)) {
        if (p.size() < 4) return;
        int ret = ats_net_cellular_set_signal(static_cast<int>(readInt32Le(data, 0)));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetSignal,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetSignal)) {
        int val = ats_net_cellular_get_signal();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetSignal,
            buildInt32Response(static_cast<qint32>(val))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetImsi)) {
        if (p.size() < 2) return;
        const quint8 len = static_cast<quint8>(data[0]);
        if (p.size() < 1 + len) return;
        const QByteArray val = p.mid(1, len);
        int ret = ats_net_cellular_set_imsi(val.data());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetImsi,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetImsi)) {
        char *imsi = ats_net_cellular_get_imsi();
        QByteArray respPayload;
        if (imsi) {
            respPayload = QByteArray(imsi, static_cast<int>(strlen(imsi)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetImsi, respPayload));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularSetImei)) {
        if (p.size() < 2) return;
        const quint8 len = static_cast<quint8>(data[0]);
        if (p.size() < 1 + len) return;
        const QByteArray val = p.mid(1, len);
        int ret = ats_net_cellular_set_imei(val.data());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularSetImei,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetImei)) {
        char *imei = ats_net_cellular_get_imei();
        QByteArray respPayload;
        if (imei) {
            respPayload = QByteArray(imei, static_cast<int>(strlen(imei)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetImei, respPayload));
        return;
    }
}

void RpcSerialServer::handleAudioFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client || frame.frameType != RpcProtocol::kFrameTypeRequest) {
        return;
    }

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandSetVolume)) {
        if (p.size() < 4) return;
        const size_t volume = static_cast<size_t>(readInt32Le(data, 0));
        int ret = ats_audio_set_volume(volume);
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandSetVolume,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandGetVolume)) {
        size_t volume = 0;
        int ret = ats_audio_get_volume(&volume);
        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandGetVolume,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }
        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(volume & 0xFF));
        respPayload.append(static_cast<char>((volume >> 8) & 0xFF));
        respPayload.append(static_cast<char>((volume >> 16) & 0xFF));
        respPayload.append(static_cast<char>((volume >> 24) & 0xFF));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandGetVolume, respPayload));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandPlayFile)) {
        if (p.size() < 2) return;
        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + pathLen) return;
        const QByteArray path = p.mid(1, pathLen);
        int ret = ats_audio_play_file(path.constData());
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandPlayFile,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandInit)) {
        int ret = ats_audio_init();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandInit,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandShutdown)) {
        ats_audio_shutdown();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandShutdown));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandIsPlaying)) {
        bool playing = ats_audio_is_playing();
        QByteArray respPayload;
        respPayload.append(playing ? '\x01' : '\x00');
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandIsPlaying, respPayload));
        return;
    }
}

void RpcSerialServer::handleReaderFrame(const RpcProtocol::Frame &frame)
{
    if (!m_client || frame.frameType != RpcProtocol::kFrameTypeRequest) {
        return;
    }

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandInit)) {
        int ret = ats_reader_init();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandInit,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandOpen)) {
        int ret = ats_reader_open();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandOpen,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandClose)) {
        int ret = ats_reader_close();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandClose,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPoll)) {
        if (p.size() < 4)
            return;

        const unsigned int timeoutMs = static_cast<unsigned int>(readInt32Le(data, 0));
        EMVInterfaceType cardInterface = EMV_INTERFACE_NONE;

        int ret = ats_reader_poll(&cardInterface, timeoutMs);

        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPoll,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(cardInterface));
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPoll, respPayload));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandCancel)) {
        int ret = ats_reader_cancel();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandCancel,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccPowerOn)) {
        unsigned char atr[64];
        size_t atrLen = sizeof(atr);
        int ret = ats_reader_icc_power_on(atr, &atrLen);

        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOn,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(atrLen & 0xFF));
        respPayload.append(static_cast<char>((atrLen >> 8) & 0xFF));
        if (atrLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(atr),
                                          static_cast<int>(atrLen)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOn, respPayload));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccPowerOff)) {
        int ret = ats_reader_icc_power_off();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOff,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccTransceiveApdu)) {
        if (p.size() < 4)
            return;

        const quint32 cmdLen = static_cast<quint32>(readInt32Le(data, 0));
        if (p.size() < static_cast<int>(4 + cmdLen))
            return;

        const QByteArray cmd = p.mid(4, static_cast<int>(cmdLen));
        unsigned char response[4096];
        size_t respLen = sizeof(response);

        int ret = ats_reader_icc_transceive_apdu(
            reinterpret_cast<const unsigned char *>(cmd.constData()),
            static_cast<size_t>(cmdLen),
            response, &respLen);

        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(respLen & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 8) & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 16) & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 24) & 0xFF));
        if (respLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(response),
                                          static_cast<int>(respLen)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu, respPayload));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccActivate)) {
        unsigned char ats[64];
        size_t atsLen = sizeof(ats);
        int ret = ats_reader_picc_activate(ats, &atsLen);

        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccActivate,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(atsLen & 0xFF));
        respPayload.append(static_cast<char>((atsLen >> 8) & 0xFF));
        if (atsLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(ats),
                                          static_cast<int>(atsLen)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccActivate, respPayload));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccDeactivate)) {
        int ret = ats_reader_picc_deactivate();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccDeactivate,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccTransceiveApdu)) {
        if (p.size() < 4)
            return;

        const quint32 cmdLen = static_cast<quint32>(readInt32Le(data, 0));
        if (p.size() < static_cast<int>(4 + cmdLen))
            return;

        const QByteArray cmd = p.mid(4, static_cast<int>(cmdLen));
        unsigned char response[4096];
        size_t respLen = sizeof(response);

        int ret = ats_reader_picc_transceive_apdu(
            reinterpret_cast<const unsigned char *>(cmd.constData()),
            static_cast<size_t>(cmdLen),
            response, &respLen);

        if (ret != 0) {
            m_client->write(RpcProtocol::buildResponseFrame(
                RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu,
                buildInt32Response(static_cast<qint32>(ret))));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(respLen & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 8) & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 16) & 0xFF));
        respPayload.append(static_cast<char>((respLen >> 24) & 0xFF));
        if (respLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(response),
                                          static_cast<int>(respLen)));
        }
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu, respPayload));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandGetLastHwError)) {
        int ret = ats_reader_get_last_hw_error();
        m_client->write(RpcProtocol::buildResponseFrame(
            RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandGetLastHwError,
            buildInt32Response(static_cast<qint32>(ret))));
        return;
    }
}
