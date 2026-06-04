#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// 前置声明 Lua 状态（避免在头文件中直接包含 lua.h）
struct lua_State;

/**
 * @brief Lua 脚本引擎
 *
 * 功能：
 *  - 创建 Lua 虚拟机（lua_State）
 *  - 向 Lua 注册全部 ATS C API（ats.* 命名空间）
 *  - 加载并执行 .lua 测试脚本
 *  - 将 Lua print() / ats.log() 桥接到 LogManager
 *
 * 使用方式：
 *  LuaEngine engine;
 *  engine.loadScript("path/to/test.lua");   // 加载脚本文件
 *  bool ok = engine.runFunction("run");     // 调用脚本中的 run()
 *
 * Lua 侧可用 API（详见 registerAtsApi 实现）：
 *  ats.sleep(ms)
 *  ats.log(msg)
 *  ats.net_set_status(bool)   → 0:ok <0:fail
 *  ats.net_get_status()       → bool
 *  ats.assert_true(cond, msg)
 *  ats.assert_eq(a, b, msg)
 */
class LuaEngine : public QObject
{
    Q_OBJECT

public:
    explicit LuaEngine(QObject *parent = nullptr);
    ~LuaEngine() override;

    /**
     * @brief 加载 Lua 脚本文件
     * @param path 脚本绝对/相对路径
     * @return true 加载成功
     */
    bool loadScript(const QString &path);

    /**
     * @brief 调用脚本中的全局函数
     * @param funcName 函数名（通常为 "run"）
     * @return true = 函数返回 true；false = 失败或断言未通过
     */
    bool runFunction(const QString &funcName = "run");

    /**
     * @brief 最近一次错误信息（脚本错误或断言失败）
     */
    QString lastError() const { return m_lastError; }

signals:
    /** 脚本内调用 ats.log() 时发出，转发给 UI 日志面板 */
    void scriptLog(const QString &msg);

private:
    void  openLibs();
    void  registerAtsApi();

    static LuaEngine *instanceFromState(lua_State *L);

    lua_State  *m_L        = nullptr;
    QString     m_lastError;

    /* ── 静态 Lua C 函数（通过 upvalue 拿到 LuaEngine*）── */
    static int lua_ats_sleep           (lua_State *L);
    static int lua_ats_log             (lua_State *L);
    static int lua_ats_net_set_status  (lua_State *L);
    static int lua_ats_net_get_status  (lua_State *L);
    static int lua_ats_assert_true     (lua_State *L);
    static int lua_ats_assert_eq       (lua_State *L);
    static int lua_ats_tick_get        (lua_State *L);
    
    /* ── 打印机 API ── */
    static int lua_ats_printer_open            (lua_State *L);
    static int lua_ats_printer_close           (lua_State *L);
    static int lua_ats_printer_start           (lua_State *L);
    static int lua_ats_printer_set_align       (lua_State *L);
    static int lua_ats_printer_set_font_size   (lua_State *L);
    static int lua_ats_printer_print_data      (lua_State *L);
    static int lua_ats_printer_print_bitmap    (lua_State *L);
    static int lua_ats_printer_set_paper_status(lua_State *L);
    static int lua_ats_printer_get_paper_status(lua_State *L);
};
