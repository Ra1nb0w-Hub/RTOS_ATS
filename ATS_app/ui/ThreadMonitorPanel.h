#pragma once

#include <QDialog>
#include <QTimer>
#include <QMap>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QList>

// Qt Charts forward declarations (Qt 5 compatible, no namespace)
class QChart;
class QChartView;
class QGraphicsSimpleTextItem;
class QGridLayout;
class QLabel;
class QLineSeries;
class QValueAxis;

#include "core/RpcProtocol.h"

class RpcFrameProcessor;

class ThreadMonitorPanel : public QDialog
{
    Q_OBJECT

public:
    explicit ThreadMonitorPanel(QWidget *parent = nullptr);
    ~ThreadMonitorPanel() override;

    void startMonitoring(RpcFrameProcessor *processor);
    void stopMonitoring();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTimerTick();
    void onThreadInfoReceived(const QVector<RpcProtocol::ThreadInfoEntry> &entries);

private:
    void setupChart();
    void clearData();
    void rebuildLegend();

    RpcFrameProcessor *m_processor = nullptr;
    QTimer *m_timer = nullptr;

    // Chart components
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    // Per-thread data: name -> [(x_time, y_percent)]
    QMap<QString, QVector<QPointF>> m_threadData;
    // Per-thread series
    QMap<QString, QLineSeries *> m_threadSeries;
    // Per-thread text labels (shows latest value at end of line)
    QMap<QString, QGraphicsSimpleTextItem *> m_threadLabels;

    // Custom legend (5 items per row)
    QWidget *m_legendWidget = nullptr;
    QGridLayout *m_legendLayout = nullptr;
    // Heap series and data (same chart as threads)
    QLineSeries *m_heapSeries = nullptr;
    QVector<QPointF> m_heapData;
    // Per-thread legend info: name -> {color, label widget}
    struct LegendItem {
        QColor color;
        QString text;
        QWidget *widget = nullptr;
    };
    QMap<QString, LegendItem> m_legendItems;
    static constexpr int kLegendColumns = 5;

    // Color palette for threads
    QList<QColor> m_colorPalette;
    int m_nextColorIndex = 0;

    static constexpr int kMaxDataPoints = 60;
    int m_timeCounter = 0;
    bool m_monitoring = false;
};
