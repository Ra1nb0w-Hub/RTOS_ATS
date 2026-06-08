#include "ThreadMonitorPanel.h"
#include "core/RpcFrameProcessor.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QGraphicsSimpleTextItem>
#include <QFont>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QSet>
#include <QtMath>

ThreadMonitorPanel::ThreadMonitorPanel(QWidget *parent)
    : QDialog(parent)
    , m_timer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("线程监控"));
    resize(900, 600);
    setMinimumSize(900, 600);

    // Initialize color palette with distinct colors
    m_colorPalette = {
        QColor(0xE6194B),   // Red
        QColor(0x3CB44B),   // Green
        QColor(0x4363D8),   // Blue
        QColor(0xF58231),   // Orange
        QColor(0x911EB4),   // Purple
        QColor(0x42D4F4),   // Cyan
        QColor(0xF032E6),   // Magenta
        QColor(0xBFEF45),   // Lime
        QColor(0xFABED4),   // Pink
        QColor(0x469990),   // Teal
        QColor(0xDCBEFF),   // Lavender
        QColor(0x9A6324),   // Brown
        QColor(0xFFFAC8),   // Beige
        QColor(0x800000),   // Maroon
        QColor(0xAAFFC3),   // Mint
        QColor(0x808000),   // Olive
    };

    setupChart();

    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ThreadMonitorPanel::onTimerTick);
}

ThreadMonitorPanel::~ThreadMonitorPanel()
{
    stopMonitoring();
}

void ThreadMonitorPanel::setupChart()
{
    m_chart = new QChart();
    m_chart->setTitle(QStringLiteral("各线程栈空间最高水位监控"));
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->hide();
    m_chart->setMargins(QMargins(4, 4, 4, 4));

    // X axis: time (sample index)
    m_axisX = new QValueAxis();
    m_axisX->setTitleText(QStringLiteral("时间(s)"));
    m_axisX->setLabelFormat("%d");
    m_axisX->setRange(0, kMaxDataPoints - 1);
    m_axisX->setTickCount(7);

    // Y axis: memory usage percentage
    m_axisY = new QValueAxis();
    m_axisY->setTitleText(QStringLiteral("占用百分比(%)"));
    m_axisY->setRange(0, 100);
    m_axisY->setLabelFormat("%d%%");
    m_axisY->setTickCount(6);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    // Custom legend widget (5 per row)
    m_legendWidget = new QWidget();
    m_legendLayout = new QGridLayout(m_legendWidget);
    m_legendLayout->setContentsMargins(8, 2, 8, 2);
    m_legendLayout->setSpacing(4);
    m_legendLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_chartView);
    layout->addWidget(m_legendWidget);
    setLayout(layout);

    rebuildLegend();
}

void ThreadMonitorPanel::startMonitoring(RpcFrameProcessor *processor)
{
    if (m_monitoring)
        return;

    m_processor = processor;
    m_monitoring = true;

    qRegisterMetaType<QVector<RpcProtocol::ThreadInfoEntry>>("QVector<RpcProtocol::ThreadInfoEntry>");

    connect(m_processor, &RpcFrameProcessor::threadInfoReceived,
            this, &ThreadMonitorPanel::onThreadInfoReceived,
            Qt::QueuedConnection);

    m_timer->start();
}

void ThreadMonitorPanel::stopMonitoring()
{
    if (!m_monitoring)
        return;

    m_timer->stop();
    m_monitoring = false;

    if (m_processor) {
        disconnect(m_processor, &RpcFrameProcessor::threadInfoReceived,
                   this, &ThreadMonitorPanel::onThreadInfoReceived);
        m_processor = nullptr;
    }

    clearData();
}

void ThreadMonitorPanel::closeEvent(QCloseEvent *event)
{
    stopMonitoring();
    event->accept();
}

void ThreadMonitorPanel::clearData()
{
    // Remove all series from chart
    for (auto it = m_threadSeries.begin(); it != m_threadSeries.end(); ++it) {
        m_chart->removeSeries(it.value());
        delete it.value();
    }
    m_threadSeries.clear();
    for (auto it = m_threadLabels.begin(); it != m_threadLabels.end(); ++it) {
        m_chart->scene()->removeItem(it.value());
        delete it.value();
    }
    m_threadLabels.clear();
    m_threadData.clear();

    // Clear heap series and label
    if (m_heapSeries) {
        m_heapSeries->clear();
    }
    m_heapData.clear();

    // Clear custom legend
    if (m_legendLayout) {
        while (QLayoutItem *item = m_legendLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                delete w;
            delete item;
        }
    }
    m_legendItems.clear();
    m_nextColorIndex = 0;
    m_timeCounter = 0;
}

void ThreadMonitorPanel::rebuildLegend()
{
    // Remove all existing items from layout
    while (QLayoutItem *item = m_legendLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }

    int row = 0, col = 0;
    for (auto it = m_legendItems.begin(); it != m_legendItems.end(); ++it) {
        const LegendItem &li = it.value();

        // Color square
        QWidget *colorBox = new QWidget();
        colorBox->setFixedSize(12, 12);
        colorBox->setStyleSheet(
            QString("background-color: %1; border: 1px solid gray;")
                .arg(li.color.name()));

        // Thread name label
        QLabel *label = new QLabel(li.text);
        label->setStyleSheet("font-size: 10pt;");

        // Row container
        QWidget *rowWidget = new QWidget();
        QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(3);
        rowLayout->addWidget(colorBox);
        rowLayout->addWidget(label);

        m_legendLayout->addWidget(rowWidget, row, col);
        col++;
        if (col >= kLegendColumns) {
            col = 0;
            row++;
        }
    }

    // Make trailing columns stretch to push items left
    for (int c = 0; c < kLegendColumns; c++) {
        m_legendLayout->setColumnStretch(c, (c == kLegendColumns - 1) ? 1 : 0);
    }
}

void ThreadMonitorPanel::onTimerTick()
{
    if (!m_processor)
        return;

    m_processor->sendThreadInfoRequest();
}

void ThreadMonitorPanel::onThreadInfoReceived(const QVector<RpcProtocol::ThreadInfoEntry> &entries)
{
    if (!m_monitoring || entries.isEmpty())
        return;

    // Increment time counter; wrap when exceeding max points
    m_timeCounter++;

    // When reached max points, remove oldest data point for all threads
    if (m_timeCounter > kMaxDataPoints) {
        const double oldestX = static_cast<double>(m_timeCounter - kMaxDataPoints);
        for (auto it = m_threadData.begin(); it != m_threadData.end(); ++it) {
            QVector<QPointF> &points = it.value();
            while (!points.isEmpty() && points.first().x() < oldestX + 0.5) {
                points.removeFirst();
            }
        }
        while (!m_heapData.isEmpty() && m_heapData.first().x() < oldestX + 0.5) {
            m_heapData.removeFirst();
        }
        m_axisX->setRange(m_timeCounter - kMaxDataPoints + 1, m_timeCounter);
    }

    // Track which threads are still present
    QSet<QString> activeThreads;
    bool legendChanged = false;

    for (const RpcProtocol::ThreadInfoEntry &entry : entries) {
        activeThreads.insert(entry.name);

        // Calculate memory usage percentage
        const double usagePercent = ((entry.stackSize > entry.remainingBytes)
            ? (static_cast<double>(entry.stackSize - entry.remainingBytes) / entry.stackSize * 100.0)
            : 0.0);

        // Format stack size label
        QString sizeLabel;
        if (entry.stackSize >= 1024U) {
            const double kb = static_cast<double>(entry.stackSize) / 1024.0;
            sizeLabel = (kb >= 1024.0)
                ? QString("%1 MB").arg(kb / 1024.0, 0, 'f', 1)
                : QString("%1 KB").arg(kb, 0, 'f', 1);
        } else {
            sizeLabel = QString("%1 B").arg(entry.stackSize);
        }

        const quint32 used = entry.stackSize - entry.remainingBytes;
        QString usedLabel;
        if (used >= 1024U) {
            const double kb = static_cast<double>(used) / 1024.0;
            usedLabel = (kb >= 1024.0)
                ? QString("%1 MB").arg(kb / 1024.0, 0, 'f', 1)
                : QString("%1 KB").arg(kb, 0, 'f', 1);
        } else {
            usedLabel = QString("%1 B").arg(used);
        }

        // Create series if new thread
        if (!m_threadSeries.contains(entry.name)) {
            QLineSeries *series = new QLineSeries();

            // Assign color from palette
            const QColor color = m_colorPalette[m_nextColorIndex % m_colorPalette.size()];
            m_nextColorIndex++;
            series->setColor(color);
            if (entry.name == QStringLiteral("HEAP"))
            {
                series->setPen(QPen(color, 2, Qt::DashLine));
            } else {
                series->setPen(QPen(color, 2));
            }

            m_chart->addSeries(series);
            series->attachAxis(m_axisX);
            series->attachAxis(m_axisY);

            m_threadSeries[entry.name] = series;
            m_threadData[entry.name] = QVector<QPointF>();

            // Create text label item for displaying latest value
            QGraphicsSimpleTextItem *labelItem = new QGraphicsSimpleTextItem(m_chart);
            QFont font;
            font.setPointSize(9);
            font.setBold(true);
            labelItem->setFont(font);
            labelItem->setZValue(10);

            m_threadLabels[entry.name] = labelItem;

            // Add to custom legend (text will be updated below)
            LegendItem li;
            li.color = color;
            m_legendItems[entry.name] = li;
        }

        // Update series name and legend text with latest values
        const QString legendText = QString("%1(%2/%3)").arg(entry.name, usedLabel, sizeLabel);
        m_threadSeries[entry.name]->setName(legendText);
        if (m_legendItems[entry.name].text != legendText) {
            m_legendItems[entry.name].text = legendText;
            legendChanged = true;
        }

        // Append data point
        QVector<QPointF> &dataPoints = m_threadData[entry.name];
        dataPoints.append(QPointF(static_cast<double>(m_timeCounter), usagePercent));

        // Limit per-thread data points
        while (dataPoints.size() > kMaxDataPoints) {
            dataPoints.removeFirst();
        }

        // Update series
        QLineSeries *series = m_threadSeries[entry.name];
        series->clear();
        for (const QPointF &pt : dataPoints) {
            series->append(pt);
        }

        // Update text label: show latest value with used/total bytes
        QGraphicsSimpleTextItem *labelItem = m_threadLabels[entry.name];
        if (!dataPoints.isEmpty()) {
            labelItem->setText(QString("%1%").arg(usagePercent, 0, 'f', 1));
            labelItem->setBrush(m_threadSeries[entry.name]->color());

            const QPointF scenePos = m_chart->mapToPosition(dataPoints.last());
            labelItem->setPos(scenePos.x() - 35, scenePos.y() - labelItem->boundingRect().height() - 5);
        }
    }

    // Remove series for threads that no longer exist
    QList<QString> toRemove;
    for (auto it = m_threadSeries.begin(); it != m_threadSeries.end(); ++it) {
        if (!activeThreads.contains(it.key())) {
            toRemove.append(it.key());
        }
    }
    for (const QString &name : toRemove) {
        m_chart->removeSeries(m_threadSeries[name]);
        delete m_threadSeries[name];
        m_threadSeries.remove(name);
        m_chart->scene()->removeItem(m_threadLabels[name]);
        delete m_threadLabels[name];
        m_threadLabels.remove(name);
        m_legendItems.remove(name);
        m_threadData.remove(name);
    }
    if (legendChanged || !toRemove.isEmpty()) {
        rebuildLegend();
    }
}
