#include "ReceiptPanel.h"

#include <QDebug>
#include <QPainter>
#include <QScrollBar>

ReceiptPanel::ReceiptPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ReceiptPanel)
{
    m_ui->setupUi(this);
}

ReceiptPanel::~ReceiptPanel()
{
    delete m_ui;
}

void ReceiptPanel::setReceiptData(const QByteArray &data, int width, int height)
{
    m_receiptData = data;
    m_receiptWidth = width;
    m_receiptHeight = height;
}

void ReceiptPanel::showReceipt()
{
    qDebug() << "[Receipt] showReceipt called, data size:" << m_receiptData.size()
             << "width:" << m_receiptWidth << "height:" << m_receiptHeight;

    if (m_receiptData.isEmpty() || m_receiptWidth <= 0 || m_receiptHeight <= 0) {
        qDebug() << "[Receipt] No valid data, skipping";
        return;
    }

    m_receiptImage = grayToQImage(
        reinterpret_cast<const unsigned char *>(m_receiptData.constData()),
        m_receiptWidth, m_receiptHeight);
    if (m_receiptImage.isNull()) {
        qDebug() << "[Receipt] grayToQImage returned null";
        return;
    }

    show();
    raise();

    int margin = 16;
    int totalW = m_receiptImage.width() + margin * 2;
    int totalH = m_receiptImage.height() + margin * 2;

    QPixmap pixmap(totalW, totalH);
    pixmap.fill(QColor(255, 255, 255));

    QPainter painter(&pixmap);
    painter.drawImage(margin, margin, m_receiptImage);
    painter.end();

    m_receiptPixmap = pixmap;

    m_ui->labelReceipt->setStyleSheet("");
    m_ui->labelReceipt->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_ui->labelReceipt->setContentsMargins(0, 0, 0, 0);
    m_ui->scrollAreaReceipt->verticalScrollBar()->setValue(0);

    scaleReceiptImage();
}

void ReceiptPanel::scaleReceiptImage()
{
    if (m_receiptPixmap.isNull())
        return;

    if (m_ui->scrollAreaReceipt == nullptr)
        return;

    int availableWidth = m_ui->scrollAreaReceipt->viewport()->width();
    if (availableWidth <= 0)
        return;

    QPixmap scaledPixmap = m_receiptPixmap.scaled(
        availableWidth,
        16777215,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    if (scaledPixmap.isNull())
        return;

    m_ui->labelReceipt->setFixedSize(scaledPixmap.size());
    m_ui->labelReceipt->setPixmap(scaledPixmap);
}

QImage ReceiptPanel::grayToQImage(const unsigned char *data, int width, int height)
{
    if (!data || width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; y++)
    {
        const unsigned char *srcLine = data + y * width;
        unsigned char *dstLine = image.scanLine(y);
        memcpy(dstLine, srcLine, width);
    }
    return image;
}
