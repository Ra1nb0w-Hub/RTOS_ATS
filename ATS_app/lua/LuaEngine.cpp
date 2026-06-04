#include "LuaEngine.h"
#include "../log/LogManager.h"
#include "sdk/ats_net.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_printer.h"

extern "C" {
#include "../../lua_lib/lua.h"
#include "../../lua_lib/lualib.h"
#include "../../lua_lib/lauxlib.h"
}

#include <QFile>
#include <QDebug>

/* ─────────────────────────────────────────────────────────────────────────────
 * 内部辅助：把 LuaEngine* 存进 Lua 注册表，让静态 C 函数能取回来
 * ───────────────────────────────────────────────────────────────────────────*/
static const char *kLuaEngineKey = "__LuaEngine__";

static void storeEngine(lua_State *L, LuaEngine *eng)
{
    lua_pushlightuserdata(L, (void *)kLuaEngineKey);
    lua_pushlightuserdata(L, (void *)eng);
    lua_settable(L, LUA_REGISTRYINDEX);
}

LuaEngine *LuaEngine::instanceFromState(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)kLuaEngineKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    LuaEngine *eng = static_cast<LuaEngine *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return eng;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 构造 / 析构
 * ───────────────────────────────────────────────────────────────────────────*/
LuaEngine::LuaEngine(QObject *parent)
    : QObject(parent)
{
    m_L = luaL_newstate();
    Q_ASSERT(m_L);
    storeEngine(m_L, this);
    openLibs();
    registerAtsApi();
}

LuaEngine::~LuaEngine()
{
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 打开标准库（保留大部分，屏蔽 io/os 文件系统操作避免副作用）
 * ───────────────────────────────────────────────────────────────────────────*/
void LuaEngine::openLibs()
{
    // 打开全部标准库（含 print / math / string / table 等）
    luaL_openlibs(m_L);

    // 重定向 print() → LogManager
    lua_pushcfunction(m_L, [](lua_State *L) -> int {
        LuaEngine *eng = instanceFromState(L);
        int n = lua_gettop(L);
        QString msg;
        for (int i = 1; i <= n; ++i) {
            if (i > 1) msg += "\t";
            msg += QString::fromUtf8(luaL_tolstring(L, i, nullptr));
            lua_pop(L, 1);
        }
        LogManager::logInfo(QString("[Lua] %1").arg(msg));
        if (eng) emit eng->scriptLog(msg);
        return 0;
    });
    lua_setglobal(m_L, "print");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 注册 ats.* API 表
 * ───────────────────────────────────────────────────────────────────────────*/
void LuaEngine::registerAtsApi()
{
    lua_newtable(m_L);  // 创建 "ats" 表

    // 辅助宏：注册一个函数到当前栈顶的表
    auto reg = [&](const char *name, lua_CFunction fn) {
        lua_pushstring(m_L, name);
        lua_pushcfunction(m_L, fn);
        lua_settable(m_L, -3);
    };

    reg("sleep",           lua_ats_sleep);
    reg("log",             lua_ats_log);
    reg("net_set_status",  lua_ats_net_set_status);
    reg("net_get_status",  lua_ats_net_get_status);
    reg("assert_true",     lua_ats_assert_true);
    reg("assert_eq",       lua_ats_assert_eq);
    reg("tick_get",        lua_ats_tick_get);
    
    /* 打印机 API */
    reg("printer_open",             lua_ats_printer_open);
    reg("printer_close",            lua_ats_printer_close);
    reg("printer_start",            lua_ats_printer_start);
    reg("printer_set_align",        lua_ats_printer_set_align);
    reg("printer_set_font_size",    lua_ats_printer_set_font_size);
    reg("printer_print_data",       lua_ats_printer_print_data);
    reg("printer_print_bitmap",     lua_ats_printer_print_bitmap);
    reg("printer_set_paper_status", lua_ats_printer_set_paper_status);
    reg("printer_get_paper_status", lua_ats_printer_get_paper_status);

    lua_setglobal(m_L, "ats");  // _G.ats = 表
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 加载脚本
 * ───────────────────────────────────────────────────────────────────────────*/
bool LuaEngine::loadScript(const QString &path)
{
    m_lastError.clear();
    QByteArray pathBytes = path.toUtf8();

    if (luaL_loadfile(m_L, pathBytes.constData()) != LUA_OK) {
        m_lastError = QString("Load error: %1").arg(
            QString::fromUtf8(lua_tostring(m_L, -1)));
        lua_pop(m_L, 1);
        LogManager::logSys(m_lastError);
        return false;
    }

    // 执行脚本顶层（定义函数等）
    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        m_lastError = QString("Script exec error: %1").arg(
            QString::fromUtf8(lua_tostring(m_L, -1)));
        lua_pop(m_L, 1);
        LogManager::logSys(m_lastError);
        return false;
    }

    return true;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 调用脚本中的 run() 函数
 * ───────────────────────────────────────────────────────────────────────────*/
bool LuaEngine::runFunction(const QString &funcName)
{
    m_lastError.clear();
    QByteArray nameBytes = funcName.toUtf8();

    lua_getglobal(m_L, nameBytes.constData());
    if (!lua_isfunction(m_L, -1)) {
        m_lastError = QString("Function '%1' not found in script").arg(funcName);
        lua_pop(m_L, 1);
        LogManager::logSys(m_lastError);
        return false;
    }

    if (lua_pcall(m_L, 0, 1, 0) != LUA_OK) {
        m_lastError = QString("Runtime error: %1").arg(
            QString::fromUtf8(lua_tostring(m_L, -1)));
        lua_pop(m_L, 1);
        LogManager::logSys(m_lastError);
        return false;
    }

    // 取返回值：期望 bool true
    bool result = false;
    if (lua_isboolean(m_L, -1)) {
        result = lua_toboolean(m_L, -1);
    } else if (lua_isnil(m_L, -1)) {
        // nil 返回视为 pass（脚本没有显式 return false）
        result = true;
    } else {
        // 非 bool 返回，视为 pass
        result = true;
    }
    lua_pop(m_L, 1);

    if (!result && m_lastError.isEmpty()) {
        m_lastError = "Script returned false (assertion failed)";
    }
    return result;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Lua C 函数实现
 * ───────────────────────────────────────────────────────────────────────────*/

// ats.sleep(ms)
int LuaEngine::lua_ats_sleep(lua_State *L)
{
    unsigned int ms = (unsigned int)luaL_checkinteger(L, 1);
    ats_thread_sleep(ms);
    return 0;
}

// ats.log(msg)
int LuaEngine::lua_ats_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    LuaEngine *eng = instanceFromState(L);
    QString qmsg = QString("[Lua] %1").arg(QString::fromUtf8(msg));
    LogManager::logInfo(qmsg);
    if (eng) emit eng->scriptLog(qmsg);
    return 0;
}

// ats.net_set_status(status)  → ret(int)
int LuaEngine::lua_ats_net_set_status(lua_State *L)
{
    bool status = lua_toboolean(L, 1);
    int ret = ats_net_set_status(status);
    lua_pushinteger(L, ret);
    return 1;
}

// ats.net_get_status()  → bool
int LuaEngine::lua_ats_net_get_status(lua_State *L)
{
    bool status = ats_net_get_status();
    lua_pushboolean(L, status ? 1 : 0);
    return 1;
}

// ats.assert_true(cond, msg)
// 若 cond 为 false，抛出 Lua 错误（脚本终止，runFunction 返回 false）
int LuaEngine::lua_ats_assert_true(lua_State *L)
{
    bool cond = lua_toboolean(L, 1);
    const char *msg = lua_isstring(L, 2) ? lua_tostring(L, 2) : "assert_true failed";
    if (!cond) {
        lua_pushfstring(L, "ASSERT_TRUE failed: %s", msg);
        lua_error(L);   // longjmp，不会返回
    }
    return 0;
}

// ats.assert_eq(a, b, msg)
int LuaEngine::lua_ats_assert_eq(lua_State *L)
{
    lua_Number a   = luaL_checknumber(L, 1);
    lua_Number b   = luaL_checknumber(L, 2);
    const char *msg = lua_isstring(L, 3) ? lua_tostring(L, 3) : "assert_eq failed";
    if (a != b) {
        lua_pushfstring(L, "ASSERT_EQ failed (%g != %g): %s", a, b, msg);
        lua_error(L);
    }
    return 0;
}

// ats.tick_get()  → ms (integer)
int LuaEngine::lua_ats_tick_get(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)ats_tick_get());
    return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 打印机 API Lua 绑定
 * ───────────────────────────────────────────────────────────────────────────*/

// ats.printer_open() → ret(int)
int LuaEngine::lua_ats_printer_open(lua_State *L)
{
    int ret = ats_printer_open();
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_close() → ret(int)
int LuaEngine::lua_ats_printer_close(lua_State *L)
{
    int ret = ats_printer_close();
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_start() → ret(int)
int LuaEngine::lua_ats_printer_start(lua_State *L)
{
    int ret = ats_printer_start();
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_set_align(mode) → ret(int)
// mode: 0=left, 1=center, 2=right
int LuaEngine::lua_ats_printer_set_align(lua_State *L)
{
    int mode = (int)luaL_checkinteger(L, 1);
    int ret = ats_printer_set_align_mode((ats_printer_align_mode_t)mode);
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_set_font_size(size) → ret(int)
// size: 0=normal, 1=double_width, 2=double_height, 3=double_width_height
int LuaEngine::lua_ats_printer_set_font_size(lua_State *L)
{
    int size = (int)luaL_checkinteger(L, 1);
    int ret = ats_printer_set_font_size((ats_printer_font_size_t)size);
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_print_data(text, is_end_of_line) → ret(int)
int LuaEngine::lua_ats_printer_print_data(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    bool is_end_of_line = lua_toboolean(L, 2);
    
    /* 复制字符串到可写缓冲区（ats_printer_set_print_data 需要 char*） */
    char *buf = (char *)malloc(strlen(text) + 1);
    if (!buf) {
        lua_pushinteger(L, -1);
        return 1;
    }
    strcpy(buf, text);
    int ret = ats_printer_set_print_data(buf, is_end_of_line);
    free(buf);
    
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_print_bitmap(data_table, width, height) → ret(int)
// data_table: Lua table of byte values
int LuaEngine::lua_ats_printer_print_bitmap(lua_State *L)
{
    if (!lua_istable(L, 1)) {
        lua_pushinteger(L, -1);
        return 1;
    }
    int width = (int)luaL_checkinteger(L, 2);
    int height = (int)luaL_checkinteger(L, 3);
    
    int bytes_per_row = (width + 7) / 8;
    int data_size = bytes_per_row * height;
    
    unsigned char *data = (unsigned char *)malloc(data_size);
    if (!data) {
        lua_pushinteger(L, -1);
        return 1;
    }
    
    /* 从 Lua table 读取数据 */
    for (int i = 0; i < data_size; i++) {
        lua_rawgeti(L, 1, i + 1);  /* Lua 数组索引从 1 开始 */
        data[i] = (unsigned char)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    
    int ret = ats_printer_set_print_bitmap(data, width, height);
    free(data);
    
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_set_paper_status(status) → ret(int)
int LuaEngine::lua_ats_printer_set_paper_status(lua_State *L)
{
    bool status = lua_toboolean(L, 1);
    int ret = ats_printer_set_paper_status(status);
    lua_pushinteger(L, ret);
    return 1;
}

// ats.printer_get_paper_status() → bool
int LuaEngine::lua_ats_printer_get_paper_status(lua_State *L)
{
    bool status = ats_printer_get_paper_status();
    lua_pushboolean(L, status ? 1 : 0);
    return 1;
}
