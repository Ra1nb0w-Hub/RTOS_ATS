#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 按键码
typedef enum {
    ATS_KEY_CODE_NONE = 0,               // 无按键

    ATS_KEY_CODE_POWER,                  // 电源
    ATS_KEY_CODE_MENU,                   // 菜单
    ATS_KEY_CODE_ENTER,                  // 确认
    ATS_KEY_CODE_CANCEL,                 // 取消
    ATS_KEY_CODE_CLEAR,                  // 清除
    ATS_KEY_CODE_STAR,                   // *号键
    ATS_KEY_CODE_POUND,                  // #号键
    ATS_KEY_CODE_FUNC1,                  // 功能键1
    ATS_KEY_CODE_FUNC2,                  // 功能键2
    ATS_KEY_CODE_FUNC3,                  // 功能键3

    ATS_KEY_CODE_NUM0,                   // 数字键0
    ATS_KEY_CODE_NUM1,                   // 数字键1
    ATS_KEY_CODE_NUM2,                   // 数字键2
    ATS_KEY_CODE_NUM3,                   // 数字键3
    ATS_KEY_CODE_NUM4,                   // 数字键4
    ATS_KEY_CODE_NUM5,                   // 数字键5
    ATS_KEY_CODE_NUM6,                   // 数字键6
    ATS_KEY_CODE_NUM7,                   // 数字键7
    ATS_KEY_CODE_NUM8,                   // 数字键8
    ATS_KEY_CODE_NUM9,                   // 数字键9
} ats_key_code_t;

// 按键事件
typedef struct {
    ats_key_code_t key_code;             // 按键码
    bool press_status;                   // 按键状态 true: 按下, false: 弹起
} ats_keypad_event_t;

// 线程优先级
typedef enum {
    ATS_THREAD_PRIORITY_LOWEST = 0,      // 最低
    ATS_THREAD_PRIORITY_LOW,             // 低
    ATS_THREAD_PRIORITY_NORMAL,          // 普通
    ATS_THREAD_PRIORITY_HIGH,            // 高
    ATS_THREAD_PRIORITY_HIGHEST,         // 最高
} ats_thread_priority_t;

// 日期时间
typedef struct {
    unsigned int uiYear;                 // 年
    unsigned int uiMonth;                // 月
    unsigned int uiDay;                  // 日
    unsigned int uiHour;                 // 时
    unsigned int uiMinute;               // 分
    unsigned int uiSecond;               // 秒
} ats_datetime_t;

// 线程句柄
typedef void* ats_thread_handle_t;

// 互斥量句柄
typedef void *ats_mutex_handle_t;

// 信号量句柄
typedef void *ats_semaphore_handle_t;

/**
 * @brief ATS入口函数
 */
void ats_main(void);

/**
 * @brief 内存分配
 * 
 * @param size 内存大小
 */
void *ats_malloc(unsigned int size);

/**
 * @brief 内存释放
 * 
 * @param ptr 内存指针
 */
void ats_free(void *ptr);

/**
 * @brief 日志打印
 *
 * @param level 等级
 * @param string 日志字符串
 */
void ats_log_print(const char *string);

/**
 * @brief 日志打印
 * 
 * @param level 等级
 * @param format 日志格式字符串
 */
void ats_log_printf(const char *format, ...);

/**
 * @brief 设置按键事件
 * 
 * @param keyCode 按键码
 * @param status 按键状态 true: 按下, false: 弹起
 * 
 * @return 0:成功 <0:失败
 */
int ats_keypad_set_event(uint8_t keyCode, bool status);

/**
 * @brief 获取按键事件
 * 
 * @param event 按键事件
 * 
 * @return 0:成功 <0:失败
 */
int ats_keypad_get_event(ats_keypad_event_t *event);

/**
 * @brief 创建线程
 * 
 * @param handle 输出线程句柄
 * @param name 线程名称
 * @param priority 线程优先级
 * @param stackSize 线程栈大小
 * @param func 线程函数
 * @param args 线程参数
 * 
 * @return 0:成功 <0:失败
 */
int ats_thread_create(ats_thread_handle_t *handle, const char *name, ats_thread_priority_t priority, unsigned int stackSize, void (*func)(void* args), void *args);

/**
 * @brief 线程休眠/延时
 * 
 * @param ms 毫秒数
 * 
 * @return 0:成功 <0:失败
 */
int ats_thread_sleep(unsigned int ms);

/**
 * @brief 获取所有线程信息
 * 
 * @note 线程信息格式:
 * 线程名称,线程栈剩余大小,线程栈分配大小\n
 * 
 * @param buffer 线程信息缓冲区
 * @param buffer_size 线程信息缓冲区大小
 * 
 * @return 0:成功 <0:失败
 */
int ats_thread_info(char *buffer, size_t buffer_size);

/**
 * @brief 创建互斥锁
 * 
 * @param handle 输出互斥锁句柄
 * @param name 互斥锁名称
 * 
 * @return 0:成功 <0:失败
 */
int ats_mutex_create(ats_mutex_handle_t *handle, const char *name);

/**
 * @brief 锁定互斥锁
 * 
 * @param handle 互斥锁句柄
 * 
 * @return 0:成功 <0:失败
 */
int ats_mutex_lock(ats_mutex_handle_t *handle);

/**
 * @brief 解锁互斥锁
 * 
 * @param handle 互斥锁句柄
 * 
 * @return 0:成功 <0:失败
 */
int ats_mutex_unlock(ats_mutex_handle_t *handle);

/**
 * @brief 创建信号量
 * 
 * @param handle 输出信号量句柄
 * @param name 信号量名称
 * @param count 信号量计数
 * 
 * @return 0:成功 <0:失败
 */
int ats_semaphore_create(ats_semaphore_handle_t *handle, const char *name, unsigned int count);

/**
 * @brief 等待信号量
 * 
 * @param handle 信号量句柄
 * @param timeout 超时时间, 单位:毫秒
 * 
 * @return 0:成功 <0:失败
 */
int ats_semaphore_wait(ats_semaphore_handle_t *handle, unsigned int timeout);

/**
 * @brief 释放信号量
 * 
 * @param handle 信号量句柄
 * 
 * @return 0:成功 <0:失败
 */
int ats_semaphore_post(ats_semaphore_handle_t *handle);

/**
 * @brief 获取日期时间
 * 
 * @param datetime 日期时间
 * 
 * @return 0:成功 <0:失败
 * @note 当变量的未设置日期时从硬件获取UTC时间，否则获取保存在变量中的日期时间
 */
int ats_datetime_get(ats_datetime_t *datetime);

/**
 * @brief 设置日期时间
 * 
 * @param datetime 日期时间
 * 
 * @return 0:成功 <0:失败
 */
int ats_datetime_set(ats_datetime_t *datetime);

/**
 * @brief 获取时间戳
 * 
 * @return 时间戳(自1970年到至今的秒数)
 * @note 从硬件获取
 */
unsigned long ats_timestamp_get(void);

/**
 * @brief 获取tick数
 * 
 * @return 开机到现在的tick数
 * @note 从Windows平台获取真是的tick值，精确度到毫秒
 */
unsigned int ats_tick_get(void);

/**
 * @brief 获取随机数
 * 
 * @param len 随机数长度
 * @param output 输出缓冲区(每个字节取值范围0x00 ~ 0xFF，不限定是否为数字、字母、符号)
 * 
 * @return 0:成功 <0:失败
 */
int ats_random(unsigned int len, unsigned char *output);

/**
 * @brief 获取序列号
 * 
 * @return 序列号字符串
 * @note 获取保存在变量中的序列号，而不是从硬件获取，如果未设置序列号默认为空
 */
char* ats_serial_number_get(void);

/**
 * @brief 设置序列号
 * 
 * @param serial_number 序列号字符串
 * 
 * @return 0:成功 <0:失败
 */
int ats_serial_number_set(char *serial_number);

#ifdef __cplusplus
}
#endif
