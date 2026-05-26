#pragma once

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QByteArray>
#include "ui_ReceiptPanel.h"

class ReceiptPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ReceiptPanel(QWidget *parent = nullptr);
    ~ReceiptPanel();

    Ui::ReceiptPanel *ui() const { return m_ui; }

    void setReceiptData(const QByteArray &data, int width, int height);
    void scaleReceiptImage();

public slots:
    void showReceipt();

private:
    QImage grayToQImage(const unsigned char *data, int width, int height);

    Ui::ReceiptPanel *m_ui;
    QImage m_receiptImage;
    QPixmap m_receiptPixmap;
    QByteArray m_receiptData;
    int m_receiptWidth = 0;
    int m_receiptHeight = 0;
};
