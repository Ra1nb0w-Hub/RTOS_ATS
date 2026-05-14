#pragma once

#include <QObject>
#include <QString>
#include <functional>

/**
 * @brief 测试用例基类
 *
 * 用法：
 *  1. 继承此类并实现 run()
 *  2. 在 run() 内使用 ASSERT_TRUE / ASSERT_EQ 宏
 *  3. 调用 TestRunner::registerCase() 注册
 *
 * 例：
 *  class TestNetworkInit : public TestCase {
 *  public:
 *      TestNetworkInit() : TestCase("Network::Init") {}
 *      bool run() override {
 *          int ret = OPayNetworkInit();
 *          ASSERT_EQ(ret, OPAY_EC_OK);
 *          return true;
 *      }
 *  };
 */
class TestCase
{
public:
    explicit TestCase(const QString &name) : m_name(name) {}
    virtual ~TestCase() = default;

    QString name() const { return m_name; }

    /**
     * @brief 可选的显示名称，设置后 UI 优先显示此名称
     */
    QString displayName() const { return m_displayName; }
    void    setDisplayName(const QString &d) { m_displayName = d; }

    /** @brief 返回用于 UI 展示的名称（优先 displayName，其次 name） */
    QString displayText() const { return m_displayName.isEmpty() ? m_name : m_displayName; }

    /**
     * @brief 执行测试
     * @return true = PASS, false = FAIL
     */
    virtual bool run() = 0;

    /**
     * @brief 最近一次失败的原因（ASSERT 宏填入）
     */
    QString failReason() const { return m_failReason; }

protected:
    void setFailReason(const QString &r) { m_failReason = r; }

    /* ─── 断言辅助 ─────────────────────────────────────────────────────────── */

    bool assertTrue(bool cond, const char *expr, const char *file, int line) {
        if (!cond) {
            m_failReason = QString("ASSERT_TRUE(%1) failed at %2:%3")
                               .arg(expr).arg(file).arg(line);
            return false;
        }
        return true;
    }

    bool assertEq(long long a, long long b, const char *exprA, const char *exprB,
                  const char *file, int line) {
        if (a != b) {
            m_failReason = QString("ASSERT_EQ(%1==%2) failed: %3 != %4 at %5:%6")
                               .arg(exprA).arg(exprB).arg(a).arg(b).arg(file).arg(line);
            return false;
        }
        return true;
    }

    bool assertStrEq(const char *a, const char *b, const char *exprA, const char *exprB,
                     const char *file, int line) {
        if (QString(a) != QString(b)) {
            m_failReason = QString("ASSERT_STREQ(%1==%2) failed: \"%3\" != \"%4\" at %5:%6")
                               .arg(exprA).arg(exprB).arg(a).arg(b).arg(file).arg(line);
            return false;
        }
        return true;
    }

private:
    QString m_name;
    QString m_displayName;  // 可选的 UI 显示名称
    QString m_failReason;
};

/* ─── 断言宏 ─────────────────────────────────────────────────────────────────── */
#define ASSERT_TRUE(cond) \
    do { if (!assertTrue((cond), #cond, __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_FALSE(cond) \
    do { if (!assertTrue(!(cond), "!" #cond, __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_EQ(a, b) \
    do { if (!assertEq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_NE(a, b) \
    do { if (!assertEq(!((a)==(b)), 1, #a " != " #b, "true", __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_STREQ(a, b) \
    do { if (!assertStrEq((a), (b), #a, #b, __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { if (!assertTrue((ptr) != nullptr, #ptr " != null", __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_GT(a, b) \
    do { if (!assertTrue((a) > (b), #a " > " #b, __FILE__, __LINE__)) return false; } while(0)

#define ASSERT_GE(a, b) \
    do { if (!assertTrue((a) >= (b), #a " >= " #b, __FILE__, __LINE__)) return false; } while(0)
