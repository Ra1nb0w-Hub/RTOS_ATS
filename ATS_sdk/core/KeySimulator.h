#pragma once

#include <QObject>
#include <QTimer>

/**
 * @brief 按键模拟器
 *
 * 模拟设备按键事件：电源键、音量+、音量-、取消键
 * 支持三种操作：单击、双击、长按
 */
class KeySimulator : public QObject
{
    Q_OBJECT

public:
    enum KeyType {
        KEY_POWER,      // 电源键
        KEY_VOLUME_UP,  // 音量+
        KEY_VOLUME_DOWN,// 音量-
        KEY_REPLAY,     // 重播键
        KEY_CANCEL      // 取消键
    };
    Q_ENUM(KeyType)

    enum KeyAction {
        ACTION_SINGLE_CLICK,   // 单击
        ACTION_DOUBLE_CLICK,   // 双击
        ACTION_LONG_PRESS      // 长按
    };
    Q_ENUM(KeyAction)

    static KeySimulator* instance();

    // 触发按键事件（供 UI 调用）
    void triggerKey(KeyType key, KeyAction action);

    // 获取按键名称
    static QString keyName(KeyType key);
    static QString actionName(KeyAction action);

signals:
    // 按键事件信号（可被测试用例或 app 代码监听）
    void keyEvent(KeyType key, KeyAction action);
    void keyPressed(KeyType key);   // 按下瞬间
    void keyReleased(KeyType key);  // 释放瞬间

private:
    explicit KeySimulator(QObject *parent = nullptr);
    ~KeySimulator() = default;
    KeySimulator(const KeySimulator&) = delete;
    KeySimulator& operator=(const KeySimulator&) = delete;
};
