#include "ScreenPanel.h"
#include "sdk/ats_lcd.h"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QFont>

ScreenPanel::ScreenPanel(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui::ScreenPanel)
{
    m_ui->setupUi(this);

    connect(&m_screenTimer, &QTimer::timeout, this, &ScreenPanel::updateScreenDisplay);
}

ScreenPanel::~ScreenPanel()
{
    delete m_ui;
}

void ScreenPanel::startRendering()
{
    updateScreenDisplay();
    m_screenTimer.start(50);
}

void ScreenPanel::updateScreenDisplay()
{
    QLabel *label = m_ui->labelScreen;
    int areaW = label->width();
    int areaH = label->height();
    if (areaW <= 0 || areaH <= 0)
        return;

    QPixmap pixmap(areaW, areaH);
    pixmap.fill(QColor("#1a1a2e"));

    const auto *fb = ats_lcd_get_framebuffer();
    unsigned short devW = ats_lcd_get_width();
    unsigned short devH = ats_lcd_get_height();

    if (fb && devW > 0 && devH > 0)
    {
        double scaleX = (double)areaW / devW;
        double scaleY = (double)areaH / devH;
        double scale  = qMin(scaleX, scaleY);

        int displayW = (int)(devW * scale);
        int displayH = (int)(devH * scale);

        QImage img((const uchar *)fb, devW, devH, devW * 2, QImage::Format_RGB16);

        int offsetX = (areaW - displayW) / 2;
        int offsetY = (areaH - displayH) / 2;

        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRect(offsetX, offsetY, displayW, displayH), img);
        }
    }
    else
    {
        {
            QPainter painter(&pixmap);
            painter.setPen(QColor("#ffffff"));
            painter.setFont(QFont("Consolas", 16));
            painter.drawText(pixmap.rect(), Qt::AlignCenter,
                             "[ No Signal ]");
        }
    }

    label->setPixmap(pixmap);
}
