# ─── Lua 5.4 静态库子工程 ────────────────────────────────────────────────────
# 将 Lua 5.4 解释器编译为独立静态库，ATS_app 链接此库
#
# 注意：排除 lua.c / luac.c（两个带有 main() 的命令行工具入口）

TEMPLATE = lib
CONFIG  += staticlib c11
CONFIG  -= qt
TARGET   = ats_lua

INCLUDEPATH += .

SOURCES += \
    lapi.c \
    lauxlib.c \
    lbaselib.c \
    lcode.c \
    lcorolib.c \
    lctype.c \
    ldblib.c \
    ldebug.c \
    ldo.c \
    ldump.c \
    lfunc.c \
    lgc.c \
    linit.c \
    liolib.c \
    llex.c \
    lmathlib.c \
    lmem.c \
    loadlib.c \
    lobject.c \
    lopcodes.c \
    loslib.c \
    lparser.c \
    lstate.c \
    lstring.c \
    lstrlib.c \
    ltable.c \
    ltablib.c \
    ltm.c \
    lundump.c \
    lutf8lib.c \
    lvm.c \
    lzio.c
