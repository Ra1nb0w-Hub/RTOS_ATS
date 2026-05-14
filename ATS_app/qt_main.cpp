/**
 * @file main.cpp
 * @brief ATS - Automated Test Simulator
 *        Qt 应用程序入口
 */

#define VERSION "1.0.0"

#include <QApplication>
#include "ui/MainWindow.h"
#include "log/LogManager.h"
#include "sdk/ats_audio.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ATS");
    app.setApplicationDisplayName(QString("Automated Test Simulator V%1").arg(VERSION));
    app.setApplicationVersion(VERSION);
    app.setOrganizationName("Ra1nb0w");

    /* 启动主窗口 */
    MainWindow w;
    w.show();

    int ret = app.exec();

    /* 关闭音频播放子系统 */
    ats_audio_shutdown();

    return ret;
}
