# ─── ATS App 子项目 ──────────────────────────────────────────────────────────────
QT += core gui widgets network xml

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
    core/TestCase.cpp \
    core/TestRunner.cpp \
    core/AppThread.cpp \
    core/KeySimulator.cpp \
    log/LogManager.cpp \
    lua/LuaEngine.cpp \
    sdk/ats_lcd.c \
    sdk/ats_printer.c \
    sdk/ats_net.c \
    sdk/ats_audio.c \
    sdk/ats_fs.c \
    sdk/ats_sys.cpp \
    app/main.c

HEADERS += \
    ui/MainWindow.h \
    core/TestCase.h \
    core/TestRunner.h \
    core/AppThread.h \
    core/KeySimulator.h \
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

FORMS += \
    ui/MainWindow.ui

# ─── Definitions ──────────────────────────────────────────────────────────────
DEFINES += ATS_SIMULATOR \
           ATS

# ─── 部署 Lua 脚本到 exe 目录 ─────────────────────────────────────────────────
# 编译后将 scripts/ 目录复制到输出目录，使 TestRunner 能在运行时找到脚本
win32 {
    copyScripts.commands = $(COPY_DIR) \"$$shell_path($$PWD/scripts)\" \"$$shell_path($$OUT_PWD/$$LIB_SUBDIR/scripts)\"
    QMAKE_EXTRA_TARGETS += copyScripts
    POST_TARGETDEPS += copyScripts
}

# ─── Default rules ────────────────────────────────────────────────────────────
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RC_ICONS = ATS.ico
