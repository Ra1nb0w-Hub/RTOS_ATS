#include "ThreadMonitorPanel.h"
#include "core/RpcFrameProcessor.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QSet>
#include <QtMath>

ThreadMonitorPanel::ThreadMonitorPanel(QWidget *parent)
    : QDialog(parent)
    , m_timer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("线程监控"));
    resize(800, 500);
    setMinimumSize(600, 400);

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
    m_chart->setTitle(QStringLiteral("线程监控"));
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setAlignment(Qt::AlignRight);
    m_chart->legend()->setShowToolTips(true);
    m_chart->setMargins(QMargins(4, 4, 4, 4));

    // X axis: time (sample index)
    m_axisX = new QValueAxis();
    m_axisX->setTitleText(QStringLiteral("时间(s)"));
    m_axisX->setLabelFormat("%d");
    m_axisX->setRange(0, kMaxDataPoints - 1);
    m_axisX->setTickCount(7);

    // Y axis: memory usage percentage
    m_axisY = new QValueAxis();
    m_axisY->setTitleText(QStringLiteral("内存最高占用(%)"));
    m_axisY->setRange(0, 100);
    m_axisY->setLabelFormat("%d%%");
    m_axisY->setTickCount(6);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_chartView);
    setLayout(layout);
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
    m_threadData.clear();
    m_nextColorIndex = 0;
    m_timeCounter = 0;
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
        m_axisX->setRange(m_timeCounter - kMaxDataPoints + 1, m_timeCounter);
    }

    // Track which threads are still present
    QSet<QString> activeThreads;

    for (const RpcProtocol::ThreadInfoEntry &entry : entries) {
        activeThreads.insert(entry.name);

        // Calculate memory usage percentage
        const double usagePercent = ((entry.stackSize > entry.remainingBytes)
            ? (static_cast<double>(entry.stackSize - entry.remainingBytes) / entry.stackSize * 100.0)
            : 0.0);

        // Create series if new thread
        if (!m_threadSeries.contains(entry.name)) {
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

            QLineSeries *series =
                new QLineSeries();
            series->setName(QString("%1(%2)").arg(entry.name, sizeLabel));

            // Assign color from palette
            const QColor color = m_colorPalette[m_nextColorIndex % m_colorPalette.size()];
            m_nextColorIndex++;
            series->setColor(color);
            series->setPen(QPen(color, 2));

            m_chart->addSeries(series);
            series->attachAxis(m_axisX);
            series->attachAxis(m_axisY);

            m_threadSeries[entry.name] = series;
            m_threadData[entry.name] = QVector<QPointF>();
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
        m_threadData.remove(name);
    }
}
