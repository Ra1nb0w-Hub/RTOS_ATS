#include "RpcFrameProcessor.h"
#include "RpcSerialServer.h"
#include "RpcNetWorker.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>
#include <QThread>
#include <cstring>

#include "RpcProtocol.h"
#include "Addr2LineResolver.h"
#include "../log/LogManager.h"

static qint32 readInt32Le(const char *data, int offset)
{
    qint32 value = 0;

    value = static_cast<quint8>(data[offset]);
    value |= (static_cast<quint32>(static_cast<quint8>(data[offset + 1])) << 8);
    value |= (static_cast<quint32>(static_cast<quint8>(data[offset + 2])) << 16);
    value |= (static_cast<quint32>(static_cast<quint8>(data[offset + 3])) << 24);

    return value;
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

static QByteArray buildBoolResponse(bool value)
{
    QByteArray payload(1, '\0');
    payload[0] = static_cast<char>(value ? 1 : 0);
    return payload;
}

RpcFrameProcessor::RpcFrameProcessor(RpcSerialServer *server)
    : QObject(nullptr), m_server(server)
{
    m_netThread = new QThread(this);
    m_netWorker = new RpcNetWorker();
    m_netWorker->moveToThread(m_netThread);

    connect(m_netWorker, &RpcNetWorker::sockRecvFinished,
            this, &RpcFrameProcessor::onSockRecvFinished,
            Qt::QueuedConnection);
    connect(m_netWorker, &RpcNetWorker::sockConnectFinished,
            this, &RpcFrameProcessor::onSockConnectFinished,
            Qt::QueuedConnection);

    m_netThread->start();
}

RpcFrameProcessor::~RpcFrameProcessor()
{
    if (m_netThread) {
        m_netThread->quit();
        m_netThread->wait(5000);
    }
    delete m_netWorker;
    m_netWorker = nullptr;
}

void RpcFrameProcessor::setElfPath(const QString &path)
{
    m_elfPath = path;
}

void RpcFrameProcessor::dispatchFrame(const RpcProtocol::Frame &frame)
{
    m_currentRequestId = frame.requestId;

    switch (frame.service) {
    case RpcProtocol::kServiceCore:
        handleCoreFrame(frame);
        break;
    case RpcProtocol::kServiceLcd:
        handleLcdFrame(frame);
        break;
    case RpcProtocol::kServicePrinter:
        handlePrinterFrame(frame);
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

void RpcFrameProcessor::sendThreadInfoRequest()
{
    m_hostRequestId++;
    if (m_hostRequestId == 0U)
        m_hostRequestId = 1U;

    const QByteArray frame = RpcProtocol::buildRequestFrame(
        RpcProtocol::kServiceCore,
        RpcProtocol::kCoreCommandGetThreadInfo,
        m_hostRequestId);

    QMetaObject::invokeMethod(m_server, "writeFrame",
        Qt::QueuedConnection, Q_ARG(QByteArray, frame));
}

void RpcFrameProcessor::postResponse(const QByteArray &frame)
{
    QMetaObject::invokeMethod(m_server, "onProcessorWriteResponse",
        Qt::QueuedConnection, Q_ARG(QByteArray, frame));
}

void RpcFrameProcessor::postLog(const QString &msg, const QString &level)
{
    QMetaObject::invokeMethod(m_server, "logMessage",
        Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(QString, level));
}

void RpcFrameProcessor::postCrash(const QString &msg)
{
    QMetaObject::invokeMethod(m_server, "crashMessage",
        Qt::QueuedConnection, Q_ARG(QString, msg));
}

void RpcFrameProcessor::onSockRecvFinished(quint8 service, quint8 command, int sock, int received, QByteArray data, quint8 requestId)
{
    m_currentRequestId = requestId;

    QByteArray respPayload;
    /* sock 句柄 — 用于调用方校验 */
    respPayload.append(static_cast<char>(sock & 0xFF));
    respPayload.append(static_cast<char>((sock >> 8) & 0xFF));
    respPayload.append(static_cast<char>((sock >> 16) & 0xFF));
    respPayload.append(static_cast<char>((sock >> 24) & 0xFF));
    respPayload.append(static_cast<char>(received & 0xFF));
    respPayload.append(static_cast<char>((received >> 8) & 0xFF));
    respPayload.append(static_cast<char>((received >> 16) & 0xFF));
    respPayload.append(static_cast<char>((received >> 24) & 0xFF));
    if (received > 0) {
        respPayload.append(data);
    }
    sendResponse(service, command, respPayload);
}

void RpcFrameProcessor::onSockConnectFinished(quint8 service, quint8 command, int sock, int ret, quint8 requestId)
{
    m_currentRequestId = requestId;

    /* sock 句柄 — 用于调用方校验 */
    QByteArray payload;
    payload.append(static_cast<char>(sock & 0xFF));
    payload.append(static_cast<char>((sock >> 8) & 0xFF));
    payload.append(static_cast<char>((sock >> 16) & 0xFF));
    payload.append(static_cast<char>((sock >> 24) & 0xFF));
    payload.append(static_cast<char>(ret & 0xFF));
    payload.append(static_cast<char>((ret >> 8) & 0xFF));
    payload.append(static_cast<char>((ret >> 16) & 0xFF));
    payload.append(static_cast<char>((ret >> 24) & 0xFF));
    sendResponse(service, command, payload);
}

void RpcFrameProcessor::sendResponse(quint8 service, quint8 command, const QByteArray &payload)
{
    postResponse(RpcProtocol::buildResponseFrame(service, command, m_currentRequestId, payload));
}

void RpcFrameProcessor::sendResponse(quint8 service, quint8 command)
{
    postResponse(RpcProtocol::buildResponseFrame(service, command, m_currentRequestId));
}

void RpcFrameProcessor::sendEvent(quint8 service, quint8 command, const QByteArray &payload)
{
    postResponse(RpcProtocol::buildEventFrame(service, command, payload));
}

void RpcFrameProcessor::sendEvent(quint8 service, quint8 command)
{
    postResponse(RpcProtocol::buildEventFrame(service, command));
}

void RpcFrameProcessor::handleCoreFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType == RpcProtocol::kFrameTypeEvent) {
        if (frame.command == RpcProtocol::kCoreCommandCrash) {
            RpcProtocol::CrashEvent crash;
            if (RpcProtocol::decodeCrashEvent(frame, &crash) && crash.pc != 0U) {
                QString msg = QString("Fireware HardFault:");

                if (!m_elfPath.isEmpty()) {
                    Addr2LineResolver resolver(m_elfPath);
                    if (resolver.isValid()) {
                        const QString pcLoc = resolver.resolve(crash.pc);
                        if (!pcLoc.isEmpty()) {
                            msg += QString("\n[PC] %1").arg(pcLoc);
                        }
                        const QString lrLoc = resolver.resolve(crash.lr);
                        if (!lrLoc.isEmpty()) {
                            msg += QString("\n[LR] %1").arg(lrLoc);
                        }
                    } else {
                        msg += QString("\n  (addr2line 工具或 ELF 文件不可用)");
                    }
                }

                {
                    const quint8 mmfsr = crash.cfsr & 0xFFU;
                    const quint8 bfsr  = (crash.cfsr >> 8) & 0xFFU;
                    const quint16 ufsr = (crash.cfsr >> 16) & 0xFFFFU;
                    const bool forced  = (crash.hfsr >> 30) & 1U;

                    msg += QString("\n--- SCB Fault Registers ---");
                    msg += QString("\nHFSR: 0x%1  FORCED=%2")
                        .arg(crash.hfsr, 8, 16, QChar('0')).arg(forced);
                    msg += QString("\nCFSR: 0x%1  (MMFSR=%2 BFSR=%3 UFSR=%4)")
                        .arg(crash.cfsr, 8, 16, QChar('0'))
                        .arg(mmfsr, 2, 16, QChar('0'))
                        .arg(bfsr, 2, 16, QChar('0'))
                        .arg(ufsr, 4, 16, QChar('0'));

                    if (mmfsr) {
                        QStringList mm;
                        if (mmfsr & 0x01U) mm << "IACCVIOL(指令访问违例)";
                        if (mmfsr & 0x02U) mm << "DACCVIOL(数据访问违例)";
                        if (mmfsr & 0x08U) mm << "MUNSTKERR(出栈违例)";
                        if (mmfsr & 0x10U) mm << "MSTKERR(压栈违例)";
                        if (mmfsr & 0x80U) mm << QString("MMARVALID(地址:0x%1)").arg(crash.mmfar, 8, 16, QChar('0'));
                        msg += "\nMMFSR: " + mm.join(" ");
                    }

                    if (bfsr) {
                        QStringList bf;
                        if (bfsr & 0x01U) bf << "IBUSERR(指令总线错误)";
                        if (bfsr & 0x02U) bf << "PRECISERR(精确数据总线错误)";
                        if (bfsr & 0x04U) bf << "IMPRECISERR(不精确数据总线错误)";
                        if (bfsr & 0x08U) bf << "UNSTKERR(出栈总线错误)";
                        if (bfsr & 0x10U) bf << "STKERR(压栈总线错误)";
                        if (bfsr & 0x80U) bf << QString("BFARVALID(地址:0x%1)").arg(crash.bfar, 8, 16, QChar('0'));
                        msg += "\nBFSR: " + bf.join(" ");
                    }

                    if (ufsr) {
                        QStringList uf;
                        if (ufsr & 0x0001U) uf << "UNDEFINSTR(未定义指令)";
                        if (ufsr & 0x0002U) uf << "INVSTATE(无效指令状态)";
                        if (ufsr & 0x0004U) uf << "INVPC(无效PC)";
                        if (ufsr & 0x0008U) uf << "NOCP(协处理器不存在)";
                        if (ufsr & 0x0100U) uf << "UNALIGNED(非对齐访问)";
                        if (ufsr & 0x0200U) uf << "DIVBYZERO(除零)";
                        msg += "\nUFSR: " + uf.join(" ");
                    }
                }

                postCrash(msg);
            }
            return;
        }

        if (frame.command == RpcProtocol::kCoreCommandWriteLog) {
            RpcProtocol::LogEvent event;
            if (RpcProtocol::decodeLogEvent(frame, &event)) {
                postLog(event.message, "INFO");
            }
            return;
        }
    }

    if (RpcProtocol::isCoreResponse(frame, RpcProtocol::kCoreCommandGetThreadInfo)) {
        QVector<RpcProtocol::ThreadInfoEntry> entries;
        if (RpcProtocol::decodeThreadInfoResponse(frame, &entries)) {
            emit threadInfoReceived(entries);
        }
        return;
    }

    if (frame.frameType != RpcProtocol::kFrameTypeRequest)
        return;

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandGetTimestamp)) {
        unsigned long ts = ats_timestamp_get();
        quint32 val = static_cast<quint32>(ts);
        QByteArray payload(4, '\0');
        payload[0] = static_cast<char>(val & 0xFF);
        payload[1] = static_cast<char>((val >> 8) & 0xFF);
        payload[2] = static_cast<char>((val >> 16) & 0xFF);
        payload[3] = static_cast<char>((val >> 24) & 0xFF);
        sendResponse(RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetTimestamp, payload);
        return;
    }

    if (RpcProtocol::isCoreRequest(frame, RpcProtocol::kCoreCommandGetSerialNumber)) {
        char *sn = ats_serial_number_get();
        QByteArray payload;
        if (sn != nullptr && sn[0] != '\0') {
            payload = QByteArray(sn, static_cast<int>(strlen(sn)));
        }
        sendResponse(RpcProtocol::kServiceCore, RpcProtocol::kCoreCommandGetSerialNumber, payload);
        return;
    }
}

void RpcFrameProcessor::handleLcdFrame(const RpcProtocol::Frame &frame)
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

void RpcFrameProcessor::handlePrinterFrame(const RpcProtocol::Frame &frame)
{
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

    if (RpcProtocol::isPrinterEvent(frame, RpcProtocol::kPrinterCommandStart)) {
        if (ats_printer_start() != 0) {
            LogManager::logWarn("Printer start event processing failed");
        }
        return;
    }

    RpcProtocol::PrinterPrintTextEvent textEvent;
    if (RpcProtocol::decodePrinterPrintTextEvent(frame, &textEvent)) {
        ats_printer_set_align_mode((ats_printer_align_mode_t)textEvent.alignMode);
        ats_printer_set_font_size((ats_printer_font_size_t)textEvent.fontSize);
        QByteArray text = textEvent.text;
        text.append('\0');
        if (ats_printer_set_print_data(text.data(), textEvent.isEndOfLine) != 0) {
            LogManager::logWarn("Printer text event processing failed");
        }
        return;
    }

    RpcProtocol::PrinterPrintBitmapEvent bitmapEvent;
    if (RpcProtocol::decodePrinterPrintBitmapEvent(frame, &bitmapEvent)) {
        ats_printer_set_align_mode((ats_printer_align_mode_t)bitmapEvent.alignMode);
        if (ats_printer_set_print_bitmap(reinterpret_cast<unsigned char *>(bitmapEvent.bitmapData.data()),
                                         bitmapEvent.width,
                                         bitmapEvent.height) != 0) {
            LogManager::logWarn("Printer bitmap event processing failed");
        }
        return;
    }
}

void RpcFrameProcessor::handleFsFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType != RpcProtocol::kFrameTypeRequest)
        return;

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
            sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandOpen,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        qint32 handleVal = static_cast<qint32>(handle);
        respPayload.append(static_cast<char>(handleVal & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 8) & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 16) & 0xFF));
        respPayload.append(static_cast<char>((handleVal >> 24) & 0xFF));
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandOpen, respPayload);
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandClose)) {
        if (p.size() < 4)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        int ret = ats_fs_close(handle);
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandClose,
                     buildInt32Response(static_cast<qint32>(ret)));
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

        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandRead, respPayload);
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandWrite)) {
        if (p.size() < 4)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        const QByteArray writeData = p.mid(4);

        int bytesWritten = ats_fs_write(handle, writeData.constData(),
                                        static_cast<size_t>(writeData.size()));
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandWrite,
                     buildInt32Response(static_cast<qint32>(bytesWritten)));
        return;
    }

    if (RpcProtocol::isFsRequest(frame, RpcProtocol::kFsCommandSeek)) {
        if (p.size() < 9)
            return;

        const ats_fs_handle_t handle = static_cast<ats_fs_handle_t>(readInt32Le(data, 0));
        const size_t offset = static_cast<size_t>(readInt32Le(data, 4));
        const int whence = static_cast<int>(data[8]);

        int ret = ats_fs_seek(handle, offset, static_cast<ats_fs_whence_t>(whence));
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandSeek,
                     buildInt32Response(static_cast<qint32>(ret)));
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
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandSize,
                     buildInt32Response(static_cast<qint32>(ret)));
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
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandRemove,
                     buildInt32Response(static_cast<qint32>(ret)));
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
        sendResponse(RpcProtocol::kServiceFs, RpcProtocol::kFsCommandExist,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }
}

void RpcFrameProcessor::handleNetFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType != RpcProtocol::kFrameTypeRequest)
        return;

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
            sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockCreate,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }
        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        qint32 sockVal = static_cast<qint32>(sock);
        respPayload.append(static_cast<char>(sockVal & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 8) & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 16) & 0xFF));
        respPayload.append(static_cast<char>((sockVal >> 24) & 0xFF));
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockCreate, respPayload);
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
        const quint8 reqId = m_currentRequestId;

        QMetaObject::invokeMethod(m_netWorker, [this, sock, host, port, timeoutMs, reqId]() {
            m_netWorker->execSockConnect(sock, host, port, timeoutMs,
                                          RpcProtocol::kServiceNet,
                                          RpcProtocol::kNetCommandSockConnect,
                                          reqId);
        }, Qt::QueuedConnection);
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockSend)) {
        if (p.size() < 4) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        const QByteArray sendData = p.mid(4);
        int sent = ats_sock_send(sock, sendData.constData(),
                                 static_cast<unsigned int>(sendData.size()));
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockSend,
                     buildInt32Response(static_cast<qint32>(sent)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockRecv)) {
        if (p.size() < 12) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        const unsigned int bufLen = static_cast<unsigned int>(readInt32Le(data, 4));
        const unsigned int timeoutMs = static_cast<unsigned int>(readInt32Le(data, 8));
        const quint8 reqId = m_currentRequestId;

        QMetaObject::invokeMethod(m_netWorker, [this, sock, bufLen, timeoutMs, reqId]() {
            m_netWorker->execSockRecv(sock, bufLen, timeoutMs,
                                       RpcProtocol::kServiceNet,
                                       RpcProtocol::kNetCommandSockRecv,
                                       reqId);
        }, Qt::QueuedConnection);
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSockClose)) {
        if (p.size() < 4) return;
        const int sock = static_cast<int>(readInt32Le(data, 0));
        int ret = ats_sock_close(sock);
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSockClose,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandSetMode)) {
        if (p.size() < 1) return;
        int ret = ats_net_set_mode(static_cast<ats_net_mode_t>(data[0]));
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandSetMode,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetSsid)) {
        char *ssid = ats_net_wifi_get_ssid();
        QByteArray respPayload;
        if (ssid) {
            respPayload = QByteArray(ssid, static_cast<int>(strlen(ssid)));
        }
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetSsid, respPayload);
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetSignal)) {
        int signal = ats_net_wifi_get_signal();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetSignal,
                     buildInt32Response(static_cast<qint32>(signal)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandWifiGetApList)) {
        ats_net_wifi_ap_t *apList = nullptr;
        unsigned int count = 0;
        int ret = ats_net_wifi_get_ap_list(&apList, &count);
        if (ret != 0) {
            if (apList)
                free(apList);
            sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetApList,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        } else {
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

            sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiGetApList, respPayload);
            return;
        }
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetMcc)) {
        int val = ats_net_cellular_get_mcc();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetMcc,
                     buildInt32Response(static_cast<qint32>(val)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetMnc)) {
        int val = ats_net_cellular_get_mnc();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetMnc,
                     buildInt32Response(static_cast<qint32>(val)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetLac)) {
        int val = ats_net_cellular_get_lac();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetLac,
                     buildInt32Response(static_cast<qint32>(val)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetCellId)) {
        int val = ats_net_cellular_get_cell_id();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetCellId,
                     buildInt32Response(static_cast<qint32>(val)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetSignal)) {
        int val = ats_net_cellular_get_signal();
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetSignal,
                     buildInt32Response(static_cast<qint32>(val)));
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetImsi)) {
        char *imsi = ats_net_cellular_get_imsi();
        QByteArray respPayload;
        if (imsi) {
            respPayload = QByteArray(imsi, static_cast<int>(strlen(imsi)));
        }
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetImsi, respPayload);
        return;
    }

    if (RpcProtocol::isNetRequest(frame, RpcProtocol::kNetCommandCellularGetImei)) {
        char *imei = ats_net_cellular_get_imei();
        QByteArray respPayload;
        if (imei) {
            respPayload = QByteArray(imei, static_cast<int>(strlen(imei)));
        }
        sendResponse(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandCellularGetImei, respPayload);
        return;
    }
}

void RpcFrameProcessor::handleAudioFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType != RpcProtocol::kFrameTypeRequest)
        return;

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandSetVolume)) {
        if (p.size() < 4) return;
        const size_t volume = static_cast<size_t>(readInt32Le(data, 0));
        int ret = ats_audio_set_volume(volume);
        sendResponse(RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandSetVolume,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandGetVolume)) {
        size_t volume = 0;
        int ret = ats_audio_get_volume(&volume);
        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandGetVolume,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }
        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(volume & 0xFF));
        respPayload.append(static_cast<char>((volume >> 8) & 0xFF));
        respPayload.append(static_cast<char>((volume >> 16) & 0xFF));
        respPayload.append(static_cast<char>((volume >> 24) & 0xFF));
        sendResponse(RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandGetVolume, respPayload);
        return;
    }

    if (RpcProtocol::isAudioRequest(frame, RpcProtocol::kAudioCommandPlayFile)) {
        if (p.size() < 2) return;
        const quint8 pathLen = static_cast<quint8>(data[0]);
        if (p.size() < 1 + pathLen) return;
        const QByteArray path = p.mid(1, pathLen);
        int ret = ats_audio_play_file(path.constData());
        sendResponse(RpcProtocol::kServiceAudio, RpcProtocol::kAudioCommandPlayFile,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }
}

void RpcFrameProcessor::handleReaderFrame(const RpcProtocol::Frame &frame)
{
    if (frame.frameType != RpcProtocol::kFrameTypeRequest)
        return;

    const QByteArray &p = frame.payload;
    const char *data = p.constData();

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandInit)) {
        int ret = ats_reader_init();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandInit,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandOpen)) {
        int ret = ats_reader_open();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandOpen,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandClose)) {
        int ret = ats_reader_close();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandClose,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPoll)) {
        if (p.size() < 4)
            return;

        const unsigned int timeoutMs = static_cast<unsigned int>(readInt32Le(data, 0));
        EMVInterfaceType cardInterface = EMV_INTERFACE_NONE;

        int ret = ats_reader_poll(&cardInterface, timeoutMs);

        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPoll,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(cardInterface));
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPoll, respPayload);
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandCancel)) {
        int ret = ats_reader_cancel();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandCancel,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccPowerOn)) {
        unsigned char atr[64];
        size_t atrLen = sizeof(atr);
        int ret = ats_reader_icc_power_on(atr, &atrLen);

        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOn,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(atrLen & 0xFF));
        respPayload.append(static_cast<char>((atrLen >> 8) & 0xFF));
        if (atrLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(atr),
                                          static_cast<int>(atrLen)));
        }
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOn, respPayload);
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccPowerOff)) {
        int ret = ats_reader_icc_power_off();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccPowerOff,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandIccTransceiveApdu)) {
        if (p.size() < 4) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu,
                         buildInt32Response(-1));
            return;
        }

        const quint32 cmdLen = static_cast<quint32>(readInt32Le(data, 0));
        if (cmdLen > 4096U || cmdLen > static_cast<quint32>(p.size() - 4)) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu,
                         buildInt32Response(-2));
            return;
        }

        const QByteArray cmd = p.mid(4, static_cast<int>(cmdLen));
        unsigned char response[4096];
        size_t respLen = sizeof(response);

        int ret = ats_reader_icc_transceive_apdu(
            reinterpret_cast<const unsigned char *>(cmd.constData()),
            static_cast<size_t>(cmdLen),
            response, &respLen);

        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu,
                         buildInt32Response(static_cast<qint32>(ret)));
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
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandIccTransceiveApdu, respPayload);
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccActivate)) {
        unsigned char ats[64];
        size_t atsLen = sizeof(ats);
        int ret = ats_reader_picc_activate(ats, &atsLen);

        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccActivate,
                         buildInt32Response(static_cast<qint32>(ret)));
            return;
        }

        QByteArray respPayload = buildInt32Response(static_cast<qint32>(ret));
        respPayload.append(static_cast<char>(atsLen & 0xFF));
        respPayload.append(static_cast<char>((atsLen >> 8) & 0xFF));
        if (atsLen > 0) {
            respPayload.append(QByteArray(reinterpret_cast<const char *>(ats),
                                          static_cast<int>(atsLen)));
        }
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccActivate, respPayload);
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccDeactivate)) {
        int ret = ats_reader_picc_deactivate();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccDeactivate,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandPiccTransceiveApdu)) {
        if (p.size() < 4) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu,
                         buildInt32Response(-1));
            return;
        }

        const quint32 cmdLen = static_cast<quint32>(readInt32Le(data, 0));
        if (cmdLen > 4096U || cmdLen > static_cast<quint32>(p.size() - 4)) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu,
                         buildInt32Response(-2));
            return;
        }

        const QByteArray cmd = p.mid(4, static_cast<int>(cmdLen));
        unsigned char response[4096];
        size_t respLen = sizeof(response);

        int ret = ats_reader_picc_transceive_apdu(
            reinterpret_cast<const unsigned char *>(cmd.constData()),
            static_cast<size_t>(cmdLen),
            response, &respLen);

        if (ret != 0) {
            sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu,
                         buildInt32Response(static_cast<qint32>(ret)));
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
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandPiccTransceiveApdu, respPayload);
        return;
    }

    if (RpcProtocol::isReaderRequest(frame, RpcProtocol::kReaderCommandGetLastHwError)) {
        int ret = ats_reader_get_last_hw_error();
        sendResponse(RpcProtocol::kServiceReader, RpcProtocol::kReaderCommandGetLastHwError,
                     buildInt32Response(static_cast<qint32>(ret)));
        return;
    }
}

void RpcFrameProcessor::onReportPaperStatus(bool status)
{
    sendEvent(RpcProtocol::kServicePrinter, RpcProtocol::kPrinterCommandPaperStatusChange, buildBoolResponse(status));
}

void RpcFrameProcessor::onReportNetMode(ats_net_mode_t mode)
{
    sendEvent(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandModeChange, buildInt32Response(mode));
}

void RpcFrameProcessor::onReportNetStatus(bool status)
{
    sendEvent(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandStatusChange, buildBoolResponse(status));  
}

void RpcFrameProcessor::onReportWifiModuleStatus(bool status)
{
    sendEvent(RpcProtocol::kServiceNet, RpcProtocol::kNetCommandWifiModuleStatusChange, buildBoolResponse(status));
}
