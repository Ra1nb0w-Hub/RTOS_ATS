#pragma once

#include "../core/TestCase.h"
#include "LuaEngine.h"
#include <QString>
#include <QFileInfo>

/**
 * @brief 基于 Lua 脚本的测试用例
 *
 * 将一个 .lua 脚本文件包装成标准 TestCase，由 TestRunner 统一调度。
 * 用例名称由脚本文件名决定（去除路径和 .lua 后缀），例如：
 *   scripts/test_net_disconnect.lua  →  "test_net_disconnect"
 *
 * Lua 脚本约定：
 *  - 脚本顶层定义全局函数 run()，返回 true = PASS，false / 抛出错误 = FAIL
 *
 * 示例脚本：
 *  @code{.lua}
 *  function run()
 *      ats.net_set_status(false)
 *      ats.assert_true(ats.net_get_status() == false, "断网后状态应为 false")
 *      ats.sleep(10000)
 *      ats.net_set_status(true)
 *      ats.assert_true(ats.net_get_status() == true, "恢复后状态应为 true")
 *      return true
 *  end
 *  @endcode
 */
class LuaTestCase : public TestCase
{
public:
    /**
     * @param scriptPath Lua 脚本文件路径（绝对或相对 exe 的路径）
     * @param displayName 可选的显示名称，如果不指定则使用脚本文件名
     *
     * 用例名取文件名（不含 .lua 后缀），例如：
     *   /path/to/scripts/test_net_disconnect.lua  →  "test_net_disconnect"
     */
    explicit LuaTestCase(const QString &scriptPath, const QString &displayName = QString())
        : TestCase(QFileInfo(scriptPath).completeBaseName())
        , m_scriptPath(scriptPath)
    {
        if (!displayName.isEmpty()) {
            setDisplayName(displayName);
        }
    }

    /**
     * @brief 获取脚本文件名（不含路径和 .lua 后缀），用于匹配配置
     */
    QString scriptBaseName() const { return QFileInfo(m_scriptPath).completeBaseName(); }

    /**
     * @brief 获取脚本完整路径
     */
    QString scriptPath() const { return m_scriptPath; }

    bool run() override
    {
        LuaEngine engine;

        if (!engine.loadScript(m_scriptPath)) {
            setFailReason(engine.lastError());
            return false;
        }

        bool ok = engine.runFunction("run");
        if (!ok) {
            setFailReason(engine.lastError());
        }
        return ok;
    }

private:
    QString m_scriptPath;
};
