#include "RpcProtocol.h"

namespace RpcProtocol
{
static quint16 readLe16(const char *data)
{
    return static_cast<quint16>(static_cast<quint8>(data[0])) |
           (static_cast<quint16>(static_cast<quint8>(data[1])) << 8);
}

static bool isFrameForCommand(const Frame &frame, quint8 service, quint8 command)
{
    return frame.frameType == kFrameTypeEvent &&
           frame.service == service &&
           frame.command == command;
}

static bool decodeRleBytes(const QByteArray &encodedData, int expectedSize, QByteArray *decodedData)
{
    int offset = 0;

    if (!decodedData || expectedSize < 0) {
        return false;
    }

    decodedData->clear();
    decodedData->reserve(expectedSize);

    while (offset < encodedData.size() && decodedData->size() < expectedSize) {
        const quint8 control = static_cast<quint8>(encodedData[offset++]);
        if ((control & 0x80U) != 0U) {
            const int repeatCount = static_cast<int>(control & 0x7FU) + 1;
            if (offset >= encodedData.size() ||
                decodedData->size() + repeatCount > expectedSize) {
                return false;
            }

            decodedData->append(repeatCount, encodedData[offset++]);
        } else {
            const int literalCount = static_cast<int>(control) + 1;
            if ((offset + literalCount) > encodedData.size() ||
                decodedData->size() + literalCount > expectedSize) {
                return false;
            }

            decodedData->append(encodedData.constData() + offset, literalCount);
            offset += literalCount;
        }
    }

    return offset == encodedData.size() && decodedData->size() == expectedSize;
}

static bool decodeRleWords(const QByteArray &encodedData, int expectedWordCount, QVector<quint16> *decodedWords)
{
    int offset = 0;

    if (!decodedWords || expectedWordCount < 0) {
        return false;
    }

    decodedWords->clear();
    decodedWords->reserve(expectedWordCount);

    while (offset < encodedData.size() && decodedWords->size() < expectedWordCount) {
        const quint8 control = static_cast<quint8>(encodedData[offset++]);
        if ((control & 0x80U) != 0U) {
            const int repeatCount = static_cast<int>(control & 0x7FU) + 1;
            if ((offset + 2) > encodedData.size() ||
                decodedWords->size() + repeatCount > expectedWordCount) {
                return false;
            }

            const quint16 value = readLe16(encodedData.constData() + offset);
            offset += 2;
            for (int index = 0; index < repeatCount; ++index) {
                decodedWords->append(value);
            }
        } else {
            const int literalCount = static_cast<int>(control) + 1;
            const int literalBytes = literalCount * 2;
            if ((offset + literalBytes) > encodedData.size() ||
                decodedWords->size() + literalCount > expectedWordCount) {
                return false;
            }

            for (int index = 0; index < literalCount; ++index) {
                decodedWords->append(readLe16(encodedData.constData() + offset + index * 2));
            }
            offset += literalBytes;
        }
    }

    return offset == encodedData.size() && decodedWords->size() == expectedWordCount;
}

quint16 crc16Ccitt(const QByteArray &data)
{
    quint16 crc = 0xFFFFU;

    for (char byteValue : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byteValue)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<quint16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<quint16>(crc << 1U);
            }
        }
    }

    return crc;
}

QByteArray buildResponseFrame(quint8 service, quint8 command, quint16 requestId,
                              const QByteArray &payload, quint8 flags)
{
    QByteArray frame;
    frame.reserve(kHeaderSize + payload.size() + kCrcSize);
    frame.append(static_cast<char>(kSof0));
    frame.append(static_cast<char>(kSof1));
    frame.append(static_cast<char>(kVersion));
    frame.append(static_cast<char>(kFrameTypeResponse));
    frame.append(static_cast<char>(flags));
    frame.append(static_cast<char>(service));
    frame.append(static_cast<char>(command));
    frame.append(static_cast<char>(requestId & 0xFFU));
    frame.append(static_cast<char>((requestId >> 8) & 0xFFU));
    frame.append(static_cast<char>(payload.size() & 0xFF));
    frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
    frame.append(payload);

    {
        const QByteArray crcInput = frame.mid(2);
        const quint16 crc = crc16Ccitt(crcInput);
        frame.append(static_cast<char>(crc & 0xFFU));
        frame.append(static_cast<char>((crc >> 8) & 0xFFU));
    }

    return frame;
}

bool tryExtractFrame(QByteArray *buffer, Frame *frame)
{
    if (!buffer || !frame) {
        return false;
    }

    while (buffer->size() >= 2) {
        if (static_cast<quint8>((*buffer)[0]) == kSof0 &&
            static_cast<quint8>((*buffer)[1]) == kSof1) {
            break;
        }

        buffer->remove(0, 1);
    }

    if (buffer->size() < kHeaderSize + kCrcSize) {
        return false;
    }

    const char *raw = buffer->constData();
    const quint8 version = static_cast<quint8>(raw[2]);
    if (version != kVersion) {
        buffer->remove(0, 1);
        return false;
    }

    const quint16 payloadLength = readLe16(raw + 9);
    if (payloadLength > kMaxPayload) {
        buffer->remove(0, 1);
        return false;
    }

    const int frameSize = kHeaderSize + static_cast<int>(payloadLength) + kCrcSize;
    if (buffer->size() < frameSize) {
        return false;
    }

    const QByteArray crcInput = buffer->mid(2, kHeaderSize - 2 + payloadLength);
    const quint16 actualCrc = crc16Ccitt(crcInput);
    const quint16 expectedCrc = readLe16(raw + kHeaderSize + payloadLength);
    if (actualCrc != expectedCrc) {
        buffer->remove(0, 1);
        return false;
    }

    frame->frameType = static_cast<quint8>(raw[3]);
    frame->flags = static_cast<quint8>(raw[4]);
    frame->service = static_cast<quint8>(raw[5]);
    frame->command = static_cast<quint8>(raw[6]);
    frame->requestId = readLe16(raw + 7);
    frame->payload = buffer->mid(kHeaderSize, payloadLength);

    buffer->remove(0, frameSize);
    return true;
}

bool decodeLogEvent(const Frame &frame, LogEvent *event)
{
    if (!event ||
        !isFrameForCommand(frame, kServiceLog, kLogCommandWrite)) {
        return false;
    }

    if (frame.payload.size() < 3) {
        return false;
    }

    const quint8 levelLength = static_cast<quint8>(frame.payload[0]);
    if (frame.payload.size() < 1 + levelLength + 2) {
        return false;
    }

    const QByteArray levelBytes = frame.payload.mid(1, levelLength);
    const quint16 messageLength = readLe16(frame.payload.constData() + 1 + levelLength);
    if (frame.payload.size() < 3 + levelLength + messageLength) {
        return false;
    }

    const QByteArray messageBytes = frame.payload.mid(3 + levelLength, messageLength);
    event->level = QString::fromUtf8(levelBytes).trimmed();
    event->message = QString::fromUtf8(messageBytes);
    return !event->level.isEmpty();
}

bool isLcdEvent(const Frame &frame)
{
    return frame.frameType == kFrameTypeEvent && frame.service == kServiceLcd;
}

bool isCoreRequest(const Frame &frame, quint8 expectedCommand)
{
    return frame.frameType == kFrameTypeRequest &&
           frame.service == kServiceCore &&
           frame.command == expectedCommand;
}

bool decodeLcdInitEvent(const Frame &frame, LcdInitEvent *event)
{
    if (!event || !isFrameForCommand(frame, kServiceLcd, kLcdCommandInit)) {
        return false;
    }

    if (frame.payload.size() != 4) {
        return false;
    }

    event->width = readLe16(frame.payload.constData());
    event->height = readLe16(frame.payload.constData() + 2);
    return event->width > 0 && event->height > 0;
}

bool decodeLcdRectEvent(const Frame &frame, quint8 expectedCommand, LcdRectEvent *event)
{
    if (!event || !isFrameForCommand(frame, kServiceLcd, expectedCommand)) {
        return false;
    }

    if (frame.payload.size() != 10) {
        return false;
    }

    const char *data = frame.payload.constData();
    event->x = readLe16(data);
    event->y = readLe16(data + 2);
    event->width = readLe16(data + 4);
    event->height = readLe16(data + 6);
    event->color = readLe16(data + 8);
    return event->width > 0 && event->height > 0;
}

bool decodeLcdBitmap1Event(const Frame &frame, LcdBitmap1Event *event)
{
    if (!event || !isFrameForCommand(frame, kServiceLcd, kLcdCommandDraw1BitBitmap)) {
        return false;
    }

    if (frame.payload.size() < 13) {
        return false;
    }

    const char *data = frame.payload.constData();
    event->x = readLe16(data);
    event->y = readLe16(data + 2);
    event->width = readLe16(data + 4);
    event->height = readLe16(data + 6);
    event->foregroundColor = readLe16(data + 8);
    event->backgroundColor = readLe16(data + 10);
    event->isTransparent = static_cast<quint8>(data[12]) != 0;

    const int expectedBitmapBytes =
        static_cast<int>(((event->width + 7U) / 8U) * event->height);
    if (event->width == 0 || event->height == 0) {
        return false;
    }

    if (frame.payload.size() == 13 + expectedBitmapBytes) {
        event->bitmapData = frame.payload.mid(13);
        return true;
    }

    if (frame.payload.size() < 14) {
        return false;
    }

    const quint8 encoding = static_cast<quint8>(data[13]);
    const QByteArray encodedBitmap = frame.payload.mid(14);
    switch (encoding) {
    case kBitmapEncodingRaw:
        if (encodedBitmap.size() != expectedBitmapBytes) {
            return false;
        }
        event->bitmapData = encodedBitmap;
        return true;
    case kBitmapEncodingRle8:
        return decodeRleBytes(encodedBitmap, expectedBitmapBytes, &event->bitmapData);
    default:
        return false;
    }
}

bool decodeLcdBitmap16Event(const Frame &frame, LcdBitmap16Event *event)
{
    if (!event || !isFrameForCommand(frame, kServiceLcd, kLcdCommandDraw16BitBitmap)) {
        return false;
    }

    if (frame.payload.size() < 8) {
        return false;
    }

    const char *data = frame.payload.constData();
    event->x = readLe16(data);
    event->y = readLe16(data + 2);
    event->width = readLe16(data + 4);
    event->height = readLe16(data + 6);

    const int expectedPixelCount = static_cast<int>(event->width) * static_cast<int>(event->height);
    const int expectedBitmapBytes = expectedPixelCount * 2;
    if (event->width == 0 || event->height == 0) {
        return false;
    }

    if (frame.payload.size() == 8 + expectedBitmapBytes) {
        event->pixels.resize(expectedPixelCount);
        for (int index = 0; index < expectedPixelCount; ++index) {
            event->pixels[index] = readLe16(data + 8 + index * 2);
        }
        return true;
    }

    if (frame.payload.size() < 9) {
        return false;
    }

    const quint8 encoding = static_cast<quint8>(data[8]);
    const QByteArray encodedPixels = frame.payload.mid(9);
    switch (encoding) {
    case kBitmapEncodingRaw:
        if (encodedPixels.size() != expectedBitmapBytes) {
            return false;
        }

        event->pixels.resize(expectedPixelCount);
        for (int index = 0; index < expectedPixelCount; ++index) {
            event->pixels[index] = readLe16(encodedPixels.constData() + index * 2);
        }
        return true;
    case kBitmapEncodingRle16:
        return decodeRleWords(encodedPixels, expectedPixelCount, &event->pixels);
    default:
        return false;
    }
}

bool isLcdDeinitEvent(const Frame &frame)
{
    return isFrameForCommand(frame, kServiceLcd, kLcdCommandDeinit) &&
           frame.payload.isEmpty();
}

bool isPrinterEvent(const Frame &frame, quint8 expectedCommand)
{
    return isFrameForCommand(frame, kServicePrinter, expectedCommand);
}

bool isPrinterPaperStatusRequest(const Frame &frame)
{
    return frame.frameType == kFrameTypeRequest &&
           frame.service == kServicePrinter &&
           frame.command == kPrinterCommandGetPaperStatus &&
           frame.payload.isEmpty();
}

bool decodePrinterEnumEvent(const Frame &frame, quint8 expectedCommand, quint8 *value)
{
    if (!value || !isPrinterEvent(frame, expectedCommand) || frame.payload.size() != 1) {
        return false;
    }

    *value = static_cast<quint8>(frame.payload[0]);
    return true;
}

bool decodePrinterPrintTextEvent(const Frame &frame, PrinterPrintTextEvent *event)
{
    if (!event || !isPrinterEvent(frame, kPrinterCommandPrintText) || frame.payload.isEmpty()) {
        return false;
    }

    event->isEndOfLine = static_cast<quint8>(frame.payload[0]) != 0U;
    event->text = frame.payload.mid(1);
    return true;
}

bool decodePrinterPrintBitmapEvent(const Frame &frame, PrinterPrintBitmapEvent *event)
{
    if (!event || !isPrinterEvent(frame, kPrinterCommandPrintBitmap) || frame.payload.size() < 4) {
        return false;
    }

    const char *data = frame.payload.constData();
    event->width = readLe16(data);
    event->height = readLe16(data + 2);

    if (event->width == 0 || event->height == 0) {
        return false;
    }

    const int expectedBitmapBytes =
        static_cast<int>(((event->width + 7U) / 8U) * event->height);
    if (frame.payload.size() == 4 + expectedBitmapBytes) {
        event->bitmapData = frame.payload.mid(4);
        return true;
    }

    if (frame.payload.size() < 5) {
        return false;
    }

    {
        const quint8 encoding = static_cast<quint8>(data[4]);
        const QByteArray encodedBitmap = frame.payload.mid(5);
        switch (encoding) {
        case kBitmapEncodingRaw:
            if (encodedBitmap.size() != expectedBitmapBytes) {
                return false;
            }
            event->bitmapData = encodedBitmap;
            return true;
        case kBitmapEncodingRle8:
            return decodeRleBytes(encodedBitmap, expectedBitmapBytes, &event->bitmapData);
        default:
            return false;
        }
    }
}
}
