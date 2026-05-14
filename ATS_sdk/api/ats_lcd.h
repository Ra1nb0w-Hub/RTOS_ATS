#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"

/**
 * @brief LCD初始化
 *
 * @param width LCD宽度(像素)
 * @param height LCD高度(像素)
 *
 * @return 0:成功 <0:失败
 */
int ats_lcd_init(unsigned short width, unsigned short height);

/**
 * @brief LCD指定区域画矩形
 *
 * @param x 横向起始位置
 * @param y 纵向起始位置
 * @param width 宽度
 * @param height 高度
 * @param rgbColor 边框的RGB565颜色
 *
 * @return 0:成功 <0:失败
 */
int ats_lcd_draw_rectangle(unsigned short x, unsigned short y, unsigned short width, unsigned short height, unsigned short rgbColor);

/**
 * @brief LCD指定区域填充颜色
 *
 * @param x 横向起始位置
 * @param y 纵向起始位置
 * @param width 宽度
 * @param height 高度
 * @param rgbColor 填充的RGB565颜色
 *
 * @return 0:成功 <0:失败
 */
int ats_lcd_fill_rectangle(unsigned short x, unsigned short y, unsigned short width, unsigned short height, unsigned short rgbColor);

/**
 * @brief LCD指定区域画Bitmap图片
 *
 * @param x 横向起始位置
 * @param y 纵向起始位置
 * @param width 宽度
 * @param height 高度
 * @param bitMapData Bitmap图片数据
 * @param foregroundColor 前置颜色
 * @param backgroundColor 背景颜色
 * @param isTransparent 是否透明
 *
 * @return 0:成功 <0:失败
 */
int ats_lcd_draw_1bit_bitmap(unsigned short x, unsigned short y, unsigned short width, unsigned short height, unsigned char *bitMapData, unsigned short foregroundColor, unsigned short backgroundColor, bool isTransparent);

/**
 * @brief LCD指定区域画Bitmap图片
 *
 * @param x 横向起始位置
 * @param y 纵向起始位置
 * @param width 宽度
 * @param height 高度
 * @param bitMapData Bitmap图片数据
 *
 * @return 0:成功 <0:失败
 */
int ats_lcd_draw_16bit_bitmap(unsigned short x, unsigned short y, unsigned short width, unsigned short height, unsigned short *bitMapData);

/**
 * @brief 获取帧缓冲指针（供 Qt 层渲染）
 *
 * 帧缓冲格式为 RGB565，每像素 2 字节，按行存储。
 * 可直接用于构造 QImage(width, height, bytesPerLine, Format_RGB16)。
 *
 * @return 帧缓冲指针，未初始化时返回 NULL
 */
const unsigned short *ats_lcd_get_framebuffer(void);

/**
 * @brief 获取 LCD 宽度
 */
unsigned short ats_lcd_get_width(void);

/**
 * @brief 获取 LCD 高度
 */
unsigned short ats_lcd_get_height(void);

/**
 * @brief 释放帧缓冲
 */
void ats_lcd_deinit(void);

#ifdef __cplusplus
}
#endif
