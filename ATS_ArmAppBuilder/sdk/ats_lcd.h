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
 * @brief 释放帧缓冲
 */
void ats_lcd_deinit(void);

#ifdef __cplusplus
}
#endif
