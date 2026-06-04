#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    ATS_PRINTER_ALIGN_MODE_LEFT = 0,             // 靠左对齐
    ATS_PRINTER_ALIGN_MODE_CENTER,               // 居中对齐
    ATS_PRINTER_ALIGN_MODE_RIGHT,                // 靠右对齐
} ats_printer_align_mode_t;

typedef enum {
    ATS_PRINTER_FONT_SIZE_NORMAL = 0,            // 正常
    ATS_PRINTER_FONT_SIZE_DOUBLE_WIDTH,          // 2倍宽
    ATS_PRINTER_FONT_SIZE_DOUBLE_HEIGHT,         // 2倍高
    ATS_PRINTER_FONT_SIZE_DOUBLE_WIDTH_HEIGHT,   // 2倍宽高
} ats_printer_font_size_t;

typedef struct {
    void (*paper_status_change)(bool status);
    void (*show_print_content)(void);
} ats_printer_rpc_callback_t;

/**
 * @brief 注册打印机RPC回调函数
 *
 * @param callback 回调函数指针
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_rpc_register_callback(ats_printer_rpc_callback_t *callback);

/**
 * @brief 打开打印机
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_open(void);

/**
 * @brief 关闭打印机
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_close(void);

/**
 * @brief 开始打印
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_start(void);

/**
 * @brief 设置对齐方式
 *
 * @param align 对齐方式
 *
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_printer_set_align_mode(ats_printer_align_mode_t align);

/**
 * @brief 获取对齐方式
 *
 * @param align 输出对齐方式
 *
 * @return 0:成功 <0:失败
 * @note 从变量中获取
 */
int ats_printer_get_align_mode(ats_printer_align_mode_t *align);

/**
 * @brief 设置字体大小
 *
 * @param size 字体大小
 *
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_printer_set_font_size(ats_printer_font_size_t size);

/**
 * @brief 获取字体大小
 *
 * @param size 输出字体大小
 *
 * @return 0:成功 <0:失败
 * @note 从变量中获取
 */
int ats_printer_get_font_size(ats_printer_font_size_t *size);

/**
 * @brief 设置打印数据
 *
 * @param data 打印数据
 * @param is_end_of_line 是否是换行
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_set_print_data(char *data, bool is_end_of_line);

/**
 * @brief 设置打印二进制数据（单色位图）
 *
 * @param data 单色位图数据，每行按字节对齐（MSB 在左）
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 *
 * @return 0:成功 <0:失败
 */
int ats_printer_set_print_bitmap(unsigned char *data, int width, int height);

/**
 * @brief 设置打印纸状态
 *
 * @param status 打印纸状态 true:有纸 false:无纸
 *
 * @return 0:成功 <0:失败
 * @note 保存到一个变量中
 */
int ats_printer_set_paper_status(bool status);

/**
 * @brief 获取打印纸状态
 *
 * @return true:有纸 false:无纸
 * @note 从变量中获取
 */
bool ats_printer_get_paper_status(void);

/**
 * @brief 获取小票渲染缓冲区
 *
 * 缓冲区格式：每像素 1 字节（0x00=白, 0xFF=黑），按行存储。
 * 调用后缓冲区所有权仍归 printer 模块，下次 open/close 会重置。
 *
 * @param[out] width  输出小票宽度（像素）
 * @param[out] height 输出小票高度（像素）
 *
 * @return 缓冲区指针，无数据时返回 NULL
 */
const unsigned char *ats_printer_get_receipt_buffer(int *width, int *height);

#ifdef __cplusplus
}
#endif
