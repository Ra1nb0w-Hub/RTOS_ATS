#include "KeySimulator.h"
#include "log/LogManager.h"

KeySimulator* KeySimulator::instance()
{
    static KeySimulator inst;
    return &inst;
}

KeySimulator::KeySimulator(QObject *parent)
    : QObject(parent)
{
}

void KeySimulator::triggerKey(KeyType key, KeyAction action)
{
    QString msg = QString("[Key] %1 - %2").arg(keyName(key), actionName(action));
    LogManager::instance()->log(msg, "SYS");

    // 发出信号
    emit keyEvent(key, action);

    // 模拟按下/释放事件
    emit keyPressed(key);
    // 短延迟后释放（异步）
    QTimer::singleShot(50, this, [this, key]() {
        emit keyReleased(key);
    });
}

QString KeySimulator::keyName(KeyType key)
{
    switch (key) {
        case KEY_POWER:       return "Power";
        case KEY_VOLUME_UP:   return "Volume+";
        case KEY_VOLUME_DOWN: return "Volume-";
        case KEY_REPLAY:      return "Replay";
        case KEY_CANCEL:      return "Cancel";
        default:              return "Unknown";
    }
}

QString KeySimulator::actionName(KeyAction action)
{
    switch (action) {
        case ACTION_SINGLE_CLICK: return "Single Click";
        case ACTION_DOUBLE_CLICK: return "Double Click";
        case ACTION_LONG_PRESS:   return "Long Press";
        default:                  return "Unknown";
    }
}
