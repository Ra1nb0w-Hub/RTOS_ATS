-- =============================================================================
-- 测试用例：断网10秒后恢复网络
-- 文件名:   test_net_disconnect.lua
-- 对应C++:  TestNetStatus.cpp / TestNetDisconnect
--
-- ATS 可用 API：
--   ats.sleep(ms)                 -- 延时（毫秒）
--   ats.log(msg)                  -- 输出日志到 ATS 日志面板
--   ats.net_set_status(bool)      -- 设置网络状态 true=正常 false=断开
--   ats.net_get_status()          -- 读取当前网络状态
--   ats.assert_true(cond, msg)    -- 断言为真，失败时终止用例
--   ats.assert_eq(a, b, msg)      -- 断言 a == b，失败时终止用例
--   ats.tick_get()                -- 获取当前 tick（毫秒）
-- =============================================================================

TEST_NAME = "断网10秒测试（Lua）"

function run()
    ats.log("=== 断网10秒测试 开始 ===")

    -- 1. 设置网络为断开状态
    local ret = ats.net_set_status(false)
    ats.assert_eq(ret, 0, "ats_net_set_status(false) 应返回 0")
    ats.log("已断开网络")

    -- 2. 验证网络状态确实为 false
    local status = ats.net_get_status()
    ats.assert_true(status == false, "断网后 net_get_status 应返回 false")
    ats.log("网络状态验证通过：false（已断开）")

    -- 3. 等待10秒，模拟 OPay 处理断网逻辑
    ats.log("等待 10 秒...")
    local t0 = ats.tick_get()
    ats.sleep(10000)
    local elapsed = ats.tick_get() - t0
    ats.log(string.format("实际等待时间: %d ms", elapsed))
    ats.assert_true(elapsed >= 9500, string.format("等待时间应 >= 9500ms，实际 %d ms", elapsed))

    -- 4. 恢复网络
    ret = ats.net_set_status(true)
    ats.assert_eq(ret, 0, "ats_net_set_status(true) 应返回 0")
    ats.log("网络已恢复")

    -- 5. 验证恢复后状态为 true
    status = ats.net_get_status()
    ats.assert_true(status == true, "恢复后 net_get_status 应返回 true")
    ats.log("网络状态验证通过：true（已恢复）")

    ats.log("=== 断网10秒测试 通过 ===")
    return true
end
