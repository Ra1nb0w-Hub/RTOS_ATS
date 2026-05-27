#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace RpcProtocol
{
static constexpr quint8 kSof0 = 0xA5;
static constexpr quint8 kSof1 = 0x5A;
static constexpr quint8 kVersion = 0x02;
static constexpr quint8 kFrameTypeRequest = 1;
static constexpr quint8 kFrameTypeResponse = 2;
static constexpr quint8 kFrameTypeEvent = 3;
static constexpr quint8 kServiceCore = 1;
static constexpr quint8 kServiceLcd = 2;
static constexpr quint8 kServicePrinter = 3;
static constexpr quint8 kCoreCommandWriteLog = 1;
static constexpr quint8 kCoreCommandCrash = 2;
static constexpr quint8 kCoreCommandSetDateTime = 3;
static constexpr quint8 kCoreCommandGetDateTime = 4;
static constexpr quint8 kCoreCommandGetTimestamp = 5;
static constexpr quint8 kCoreCommandGetSerialNumber = 6;
static constexpr quint8 kLcdCommandInit = 1;
static constexpr quint8 kLcdCommandDrawRectangle = 2;
static constexpr quint8 kLcdCommandFillRectangle = 3;
static constexpr quint8 kLcdCommandDraw1BitBitmap = 4;
static constexpr quint8 kLcdCommandDraw16BitBitmap = 5;
static constexpr quint8 kLcdCommandDeinit = 6;
static constexpr quint8 kPrinterCommandOpen = 1;
static constexpr quint8 kPrinterCommandClose = 2;
static constexpr quint8 kPrinterCommandSetAlign = 3;
static constexpr quint8 kPrinterCommandSetFontSize = 4;
static constexpr quint8 kPrinterCommandPrintText = 5;
static constexpr quint8 kPrinterCommandPrintBitmap = 6;
static constexpr quint8 kPrinterCommandSetPaperStatus = 7;
static constexpr quint8 kPrinterCommandGetPaperStatus = 8;
static constexpr quint8 kBitmapEncodingRaw = 0;
static constexpr quint8 kBitmapEncodingRle8 = 1;
static constexpr quint8 kBitmapEncodingRle16 = 2;
static constexpr int kHeaderSize = 7;

struct Frame
{
    quint8 frameType = 0;
    quint8 service = 0;
    quint8 command = 0;
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
    QByteArray text;
};

struct PrinterPrintBitmapEvent
{
    quint16 width = 0;
    quint16 height = 0;
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
};

QByteArray buildResponseFrame(quint8 service, quint8 command,
                              const QByteArray &payload = QByteArray());
bool tryExtractFrame(QByteArray *buffer, Frame *frame);
bool decodeLogEvent(const Frame &frame, LogEvent *event);
bool decodeCrashEvent(const Frame &frame, CrashEvent *event);
bool isCoreRequest(const Frame &frame, quint8 expectedCommand);
bool decodeCoreDateTimeRequest(const Frame &frame, DateTime *dt);
bool isLcdEvent(const Frame &frame);
bool decodeLcdInitEvent(const Frame &frame, LcdInitEvent *event);
bool decodeLcdRectEvent(const Frame &frame, quint8 expectedCommand, LcdRectEvent *event);
bool decodeLcdBitmap1Event(const Frame &frame, LcdBitmap1Event *event);
bool decodeLcdBitmap16Event(const Frame &frame, LcdBitmap16Event *event);
bool isLcdDeinitEvent(const Frame &frame);
bool isPrinterEvent(const Frame &frame, quint8 expectedCommand);
bool isPrinterPaperStatusRequest(const Frame &frame);
bool decodePrinterEnumEvent(const Frame &frame, quint8 expectedCommand, quint8 *value);
bool decodePrinterPrintTextEvent(const Frame &frame, PrinterPrintTextEvent *event);
bool decodePrinterPrintBitmapEvent(const Frame &frame, PrinterPrintBitmapEvent *event);
}
