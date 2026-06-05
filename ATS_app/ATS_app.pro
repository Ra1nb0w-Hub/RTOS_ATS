# ─── ATS App 子项目 ──────────────────────────────────────────────────────────────
QT += core gui widgets network xml charts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += console

TARGET = ATS
TEMPLATE = app

# ─── 链接预编译的静态库 ──────────────────────────────────────────────────────────
win32:CONFIG(debug, debug|release): LIB_SUBDIR = debug
else: LIB_SUBDIR = release

LIBS += -L$$OUT_PWD/../mbedtls_lib/$$LIB_SUBDIR -lats_mbedtls
LIBS += -L$$OUT_PWD/../mqtt_lib/$$LIB_SUBDIR -lats_mqtt
LIBS += -L$$OUT_PWD/../lua_lib/$$LIB_SUBDIR -lats_lua
LIBS += -L$$OUT_PWD/../emv_lib/$$LIB_SUBDIR -lats_emv

win32: LIBS += -lws2_32 -lole32 -loleaut32 -lwinmm -lwinscard

# ─── Include paths ────────────────────────────────────────────────────────────
INCLUDEPATH += \
    $$PWD \
    $$PWD/.. \
    $$PWD/../emv_lib/include \
    $$PWD/../mqtt_lib \
    $$PWD/../mbedtls_lib \
    $$PWD/../mbedtls_lib/mbedtls \
    $$PWD/../mbedtls_lib/mbedtls/include \
    $$PWD/../mqtt_lib/mqtt_embed/MQTTPacket/src \
    $$PWD/../lua_lib

# ─── Sources ──────────────────────────────────────────────────────────────────
SOURCES += \
    qt_main.cpp \
    ui/MainWindow.cpp \
    ui/ScreenPanel.cpp \
    ui/ButtonsPanel.cpp \
    ui/TestCasesPanel.cpp \
    ui/StatusPanel.cpp \
    ui/LogPanel.cpp \
    ui/ReceiptPanel.cpp \
    ui/ThreadMonitorPanel.cpp \
    core/TestCase.cpp \
    core/TestRunner.cpp \
    core/AppThread.cpp \
    core/QemuController.cpp \
    core/RpcProtocol.cpp \
    core/RpcSerialServer.cpp \
    core/RpcFrameProcessor.cpp \
    core/RpcNetWorker.cpp \
    core/Addr2LineResolver.cpp \
    lua/LuaEngine.cpp \
    log/LogManager.cpp \
    sdk/ats_lcd.c \
    sdk/ats_printer.c \
    sdk/ats_net.c \
    sdk/ats_audio.c \
    sdk/ats_fs.c \
    sdk/ats_sys.cpp \
    sdk/ats_reader.c \
    app/main.c

HEADERS += \
    ui/MainWindow.h \
    ui/ScreenPanel.h \
    ui/ButtonsPanel.h \
    ui/TestCasesPanel.h \
    ui/StatusPanel.h \
    ui/LogPanel.h \
    ui/ReceiptPanel.h \
    ui/ThreadMonitorPanel.h \
    core/TestCase.h \
    core/TestRunner.h \
    core/AppThread.h \
    core/QemuController.h \
    core/RpcProtocol.h \
    core/RpcSerialServer.h \
    core/RpcFrameProcessor.h \
    core/RpcNetWorker.h \
    core/Addr2LineResolver.h \
    log/LogManager.h \
    lua/LuaEngine.h \
    lua/LuaTestCase.h \
    sdk/ats_error.h \
    sdk/ats_lcd.h \
    sdk/ats_printer.h \
    sdk/ats_net.h \
    sdk/ats_audio.h \
    sdk/ats_fs.h \
    sdk/ats_sys.h \
    sdk/ats_reader.h

FORMS += \
    ui/MainWindow.ui \
    ui/ScreenPanel.ui \
    ui/ButtonsPanel.ui \
    ui/TestCasesPanel.ui \
    ui/StatusPanel.ui \
    ui/LogPanel.ui \
    ui/ReceiptPanel.ui

# ─── Definitions ──────────────────────────────────────────────────────────────
DEFINES += ATS_SIMULATOR \
           ATS

# ─── 部署 scripts、tools 目录
win32 {
    copyScripts.commands = $(COPY_DIR) /d \"$$shell_path($$PWD/scripts)\" \"$$shell_path($$OUT_PWD/$$LIB_SUBDIR/scripts)\"
    QMAKE_EXTRA_TARGETS += copyScripts
    POST_TARGETDEPS += copyScripts

    copyTools.commands = $(COPY_DIR) /d \"$$shell_path($$PWD/tools)\" \"$$shell_path($$OUT_PWD/$$LIB_SUBDIR/tools)\"
    QMAKE_EXTRA_TARGETS += copyTools
    POST_TARGETDEPS += copyTools
}

# ─── Default rules ────────────────────────────────────────────────────────────
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RC_ICONS = ATS.ico
