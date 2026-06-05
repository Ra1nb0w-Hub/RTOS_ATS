#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace RpcProtocol
{
static constexpr quint8 kSof0 = 0xA5;
static constexpr quint8 kSof1 = 0x5A;
static constexpr quint8 kFrameTypeRequest = 1;
static constexpr quint8 kFrameTypeResponse = 2;
static constexpr quint8 kFrameTypeEvent = 3;

static constexpr quint8 kServiceCore = 1;
static constexpr quint8 kServiceLcd = 2;
static constexpr quint8 kServicePrinter = 3;
static constexpr quint8 kServiceFs = 4;
static constexpr quint8 kServiceNet = 5;
static constexpr quint8 kServiceAudio = 6;
static constexpr quint8 kServiceReader = 7;

static constexpr quint8 kCoreCommandWriteLog = 1;
static constexpr quint8 kCoreCommandCrash = 2;
static constexpr quint8 kCoreCommandGetTimestamp = 3;
static constexpr quint8 kCoreCommandGetSerialNumber = 4;
static constexpr quint8 kCoreCommandGetThreadInfo = 5;

static constexpr quint8 kLcdCommandInit = 1;
static constexpr quint8 kLcdCommandDrawRectangle = 2;
static constexpr quint8 kLcdCommandFillRectangle = 3;
static constexpr quint8 kLcdCommandDraw1BitBitmap = 4;
static constexpr quint8 kLcdCommandDraw16BitBitmap = 5;
static constexpr quint8 kLcdCommandDeinit = 6;

static constexpr quint8 kPrinterCommandOpen = 1;
static constexpr quint8 kPrinterCommandClose = 2;
static constexpr quint8 kPrinterCommandStart = 3;
static constexpr quint8 kPrinterCommandPrintText = 4;
static constexpr quint8 kPrinterCommandPrintBitmap = 5;
static constexpr quint8 kPrinterCommandPaperStatusChange = 6;

static constexpr quint8 kFsCommandOpen = 1;
static constexpr quint8 kFsCommandClose = 2;
static constexpr quint8 kFsCommandRead = 3;
static constexpr quint8 kFsCommandWrite = 4;
static constexpr quint8 kFsCommandSeek = 5;
static constexpr quint8 kFsCommandSize = 6;
static constexpr quint8 kFsCommandRemove = 7;
static constexpr quint8 kFsCommandExist = 8;

static constexpr quint8 kNetCommandSockCreate = 1;
static constexpr quint8 kNetCommandSockConnect = 2;
static constexpr quint8 kNetCommandSockSend = 3;
static constexpr quint8 kNetCommandSockRecv = 4;
static constexpr quint8 kNetCommandSockClose = 5;
static constexpr quint8 kNetCommandSetMode = 6;
static constexpr quint8 kNetCommandModeChange = 7;
static constexpr quint8 kNetCommandStatusChange = 8;
static constexpr quint8 kNetCommandWifiModuleStatusChange = 9;
static constexpr quint8 kNetCommandWifiGetSsid = 10;
static constexpr quint8 kNetCommandWifiGetSignal = 11;
static constexpr quint8 kNetCommandWifiGetApList = 12;
static constexpr quint8 kNetCommandCellularGetMcc = 13;
static constexpr quint8 kNetCommandCellularGetMnc = 14;
static constexpr quint8 kNetCommandCellularGetLac = 15;
static constexpr quint8 kNetCommandCellularGetCellId = 16;
static constexpr quint8 kNetCommandCellularGetSignal = 17;
static constexpr quint8 kNetCommandCellularGetImsi = 18;
static constexpr quint8 kNetCommandCellularGetImei = 19;

static constexpr quint8 kAudioCommandSetVolume = 1;
static constexpr quint8 kAudioCommandGetVolume = 2;
static constexpr quint8 kAudioCommandPlayFile = 3;

static constexpr quint8 kReaderCommandInit = 1;
static constexpr quint8 kReaderCommandOpen = 2;
static constexpr quint8 kReaderCommandClose = 3;
static constexpr quint8 kReaderCommandPoll = 4;
static constexpr quint8 kReaderCommandCancel = 5;
static constexpr quint8 kReaderCommandIccPowerOn = 6;
static constexpr quint8 kReaderCommandIccPowerOff = 7;
static constexpr quint8 kReaderCommandIccTransceiveApdu = 8;
static constexpr quint8 kReaderCommandPiccActivate = 9;
static constexpr quint8 kReaderCommandPiccDeactivate = 10;
static constexpr quint8 kReaderCommandPiccTransceiveApdu = 11;
static constexpr quint8 kReaderCommandGetLastHwError = 12;

static constexpr quint8 kBitmapEncodingRaw = 0;
static constexpr quint8 kBitmapEncodingRle8 = 1;
static constexpr quint8 kBitmapEncodingRle16 = 2;

static constexpr int kHeaderSize = 8;

struct Frame
{
    quint8 frameType = 0;
    quint8 service = 0;
    quint8 command = 0;
    quint8 requestId = 0;
    QByteArray payload;
};

struct LogEvent
{
    QString message;
};

struct LcdInitEvent
{
    quint16 width = 0;
    quint16 height = 0;
};

struct LcdRectEvent
{
    quint16 x = 0;
    quint16 y = 0;
    quint16 width = 0;
    quint16 height = 0;
    quint16 color = 0;
};

struct LcdBitmap1Event
{
    quint16 x = 0;
    quint16 y = 0;
    quint16 width = 0;
    quint16 height = 0;
    quint16 foregroundColor = 0;
    quint16 backgroundColor = 0;
    bool isTransparent = false;
    QByteArray bitmapData;
};

struct LcdBitmap16Event
{
    quint16 x = 0;
    quint16 y = 0;
    quint16 width = 0;
    quint16 height = 0;
    QVector<quint16> pixels;
};

struct PrinterPrintTextEvent
{
    bool isEndOfLine = false;
    quint8 alignMode = 0;
    quint8 fontSize = 0;
    QByteArray text;
};

struct PrinterPrintBitmapEvent
{
    quint16 width = 0;
    quint16 height = 0;
    quint8 alignMode = 0;
    QByteArray bitmapData;
};

struct DateTime
{
    quint16 year = 0;
    quint8 month = 0;
    quint8 day = 0;
    quint8 hour = 0;
    quint8 minute = 0;
    quint8 second = 0;
};

struct CrashEvent
{
    quint32 pc = 0;
    quint32 lr = 0;
    quint32 cfsr = 0;
    quint32 hfsr = 0;
    quint32 bfar = 0;
    quint32 mmfar = 0;
};

struct ThreadInfoEntry
{
    QString name;
    quint32 remainingBytes = 0;
    quint32 stackSize = 0;
};

QByteArray buildRequestFrame(quint8 service, quint8 command, quint8 requestId, const QByteArray &payload = QByteArray());
QByteArray buildResponseFrame(quint8 service, quint8 command, quint8 requestId, const QByteArray &payload = QByteArray());
QByteArray buildEventFrame(quint8 service, quint8 command, const QByteArray &payload = QByteArray());

bool tryExtractFrame(QByteArray *buffer, Frame *frame);
bool decodeLogEvent(const Frame &frame, LogEvent *event);
bool decodeCrashEvent(const Frame &frame, CrashEvent *event);
bool decodeThreadInfoResponse(const Frame &frame, QVector<ThreadInfoEntry> *entries);
bool isCoreRequest(const Frame &frame, quint8 expectedCommand);
bool isCoreResponse(const Frame &frame, quint8 expectedCommand);
bool isLcdEvent(const Frame &frame);
bool decodeLcdInitEvent(const Frame &frame, LcdInitEvent *event);
bool decodeLcdRectEvent(const Frame &frame, quint8 expectedCommand, LcdRectEvent *event);
bool decodeLcdBitmap1Event(const Frame &frame, LcdBitmap1Event *event);
bool decodeLcdBitmap16Event(const Frame &frame, LcdBitmap16Event *event);
bool isLcdDeinitEvent(const Frame &frame);
bool isPrinterEvent(const Frame &frame, quint8 expectedCommand);
bool decodePrinterEnumEvent(const Frame &frame, quint8 expectedCommand, quint8 *value);
bool decodePrinterPrintTextEvent(const Frame &frame, PrinterPrintTextEvent *event);
bool decodePrinterPrintBitmapEvent(const Frame &frame, PrinterPrintBitmapEvent *event);
bool isFsRequest(const Frame &frame, quint8 expectedCommand);
bool isNetRequest(const Frame &frame, quint8 expectedCommand);
bool isAudioRequest(const Frame &frame, quint8 expectedCommand);
bool isReaderRequest(const Frame &frame, quint8 expectedCommand);
}

Q_DECLARE_METATYPE(RpcProtocol::ThreadInfoEntry)
