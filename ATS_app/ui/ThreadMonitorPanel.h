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

    // Color palette for threads
    QList<QColor> m_colorPalette;
    int m_nextColorIndex = 0;

    static constexpr int kMaxDataPoints = 60;
    int m_timeCounter = 0;
    bool m_monitoring = false;
};
