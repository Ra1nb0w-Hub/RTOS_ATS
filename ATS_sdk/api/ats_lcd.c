/**
 * @file ats_lcd.c
 * @brief ATS LCD 模拟 - RGB565 帧缓冲实现
 *
 * 使用内存中的 RGB565 帧缓冲模拟嵌入式 LCD 显示。
 * 通过 ats_lcd_get_framebuffer() 可获取帧数据供 Qt 层渲染到 QLabel。
 */

#include "ats_lcd.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * 帧缓冲状态
 * ========================================================= */

static unsigned short *s_framebuffer = NULL;  /* RGB565 帧缓冲 */
static unsigned short  s_width  = 0;          /* LCD 宽度 */
static unsigned short  s_height = 0;          /* LCD 高度 */

/* =========================================================
 * 内部辅助
 * ========================================================= */

/** 将 RGB565 颜色写入帧缓冲指定坐标 */
static inline void set_pixel(unsigned short x, unsigned short y, unsigned short color)
{
    s_framebuffer[y * s_width + x] = color;
}

/* =========================================================
 * 公共 API
 * ========================================================= */

int ats_lcd_init(unsigned short width, unsigned short height)
{
    if (width == 0 || height == 0)
        return -1;

    /* 释放旧的帧缓冲 */
    if (s_framebuffer)
    {
        free(s_framebuffer);
        s_framebuffer = NULL;
    }

    /* 分配 RGB565 帧缓冲（每像素 2 字节） */
    s_framebuffer = (unsigned short *)calloc(width * height, sizeof(unsigned short));
    if (!s_framebuffer)
        return -1;

    s_width  = width;
    s_height = height;

    /* 默认清屏为黑色 */
    memset(s_framebuffer, 0x00, width * height * sizeof(unsigned short));

    return 0;
}

int ats_lcd_draw_rectangle(unsigned short x, unsigned short y, unsigned short width, unsigned short height, unsigned short rgbColor)
{
    if (!s_framebuffer)
        return -1;

    if (width == 0 || height == 0)
        return -1;

    /* 越界裁剪 */
    if (x >= s_width || y >= s_height)
        return -1;

    unsigned short x_end = x + width - 1;
    unsigned short y_end = y + height - 1;

    if (x_end >= s_width)
        x_end = s_width - 1;
    if (y_end >= s_height)
        y_end = s_height - 1;

    /* 绘制上边框 */
    for (unsigned short col = x; col <= x_end; col++)
    {
        set_pixel(col, y, rgbColor);
    }

    /* 绘制下边框 */
    for (unsigned short col = x; col <= x_end; col++)
    {
        set_pixel(col, y_end, rgbColor);
    }

    /* 绘制左边框 */
    for (unsigned short row = y; row <= y_end; row++)
    {
        set_pixel(x, row, rgbColor);
    }

    /* 绘制右边框 */
    for (unsigned short row = y; row <= y_end; row++)
    {
        set_pixel(x_end, row, rgbColor);
    }

    return 0;
}

int ats_lcd_fill_rectangle(unsigned short x, unsigned short y,
                           unsigned short width, unsigned short height,
                           unsigned short rgbColor)
{
    if (!s_framebuffer)
        return -1;

    /* 越界裁剪 */
    if (x >= s_width || y >= s_height)
        return -1;

    unsigned short x_end = x + width;
    unsigned short y_end = y + height;

    if (x_end > s_width)
        x_end = s_width;
    if (y_end > s_height)
        y_end = s_height;

    /* 逐行填充 */
    for (unsigned short row = y; row < y_end; row++)
    {
        unsigned short *line = s_framebuffer + row * s_width;
        for (unsigned short col = x; col < x_end; col++)
        {
            line[col] = rgbColor;
        }
    }

    return 0;
}

int ats_lcd_draw_1bit_bitmap(unsigned short x, unsigned short y,
                        unsigned short width, unsigned short height,
                        unsigned char *bitMapData, unsigned short foregroundColor, unsigned short backgroundColor, bool isTransparent)
{
    if (!s_framebuffer || !bitMapData)
        return -1;

    if (width == 0 || height == 0)
        return -1;

    /* 越界裁剪 */
    if (x >= s_width || y >= s_height)
        return -1;

    unsigned short x_end = x + width;
    unsigned short y_end = y + height;

    if (x_end > s_width)
        x_end = s_width;
    if (y_end > s_height)
        y_end = s_height;

    unsigned short actual_width = x_end - x;
    unsigned short actual_height = y_end - y;

    /* 计算每行字节数（1bit/像素，向上取整到字节） */
    unsigned short bytes_per_row = (width + 7) / 8;

    /* 逐行逐像素绘制 */
    for (unsigned short row = 0; row < actual_height; row++)
    {
        unsigned short fb_y = y + row;
        unsigned short *dst_line = s_framebuffer + fb_y * s_width;

        for (unsigned short col = 0; col < actual_width; col++)
        {
            unsigned short fb_x = x + col;

            /* 计算该像素在 bitmap 数据中的位置 */
            unsigned short byte_offset = row * bytes_per_row + (col / 8);
            unsigned char bit_mask = 0x80 >> (col % 8);  /* MSB 在前 */

            /* 根据 bit 值决定绘制前景色或背景色 */
            if (bitMapData[byte_offset] & bit_mask)
            {
                /* bit = 1: 绘制前景色 */
                dst_line[fb_x] = foregroundColor;
            }
            else
            {
                /* bit = 0: 非透明模式下绘制背景色 */
                if (!isTransparent)
                    dst_line[fb_x] = backgroundColor;
            }
        }
    }

    return 0;
}

int ats_lcd_draw_16bit_bitmap(unsigned short x, unsigned short y,
                         unsigned short width, unsigned short height,
                         unsigned short *bitMapData)
{
    if (!s_framebuffer || !bitMapData)
        return -1;

    if (width == 0 || height == 0)
        return -1;

    /* 越界裁剪 */
    if (x >= s_width || y >= s_height)
        return -1;

    unsigned short x_end = x + width;
    unsigned short y_end = y + height;

    if (x_end > s_width)
        x_end = s_width;
    if (y_end > s_height)
        y_end = s_height;

    /* 逐行拷贝 bitmap 数据到帧缓冲 */
    for (unsigned short row = y; row < y_end; row++)
    {
        unsigned short *dst = s_framebuffer + row * s_width + x;
        const unsigned short *src = bitMapData + (row - y) * width;
        unsigned short copy_len = x_end - x;
        memcpy(dst, src, copy_len * sizeof(unsigned short));
    }

    return 0;
}

/* =========================================================
 * 帧缓冲访问（供 Qt 层渲染）
 * ========================================================= */

/**
 * @brief 获取帧缓冲指针
 *
 * 帧缓冲格式为 RGB565（每像素 2 字节，大端序：R[4:0] G[5:0] B[4:0]）。
 * 数据按行存储，每行 s_width 个像素。
 *
 * @return 帧缓冲指针，未初始化时返回 NULL
 */
const unsigned short *ats_lcd_get_framebuffer(void)
{
    return s_framebuffer;
}

/**
 * @brief 获取 LCD 宽度
 */
unsigned short ats_lcd_get_width(void)
{
    return s_width;
}

/**
 * @brief 获取 LCD 高度
 */
unsigned short ats_lcd_get_height(void)
{
    return s_height;
}

/**
 * @brief 释放帧缓冲
 */
void ats_lcd_deinit(void)
{
    if (s_framebuffer)
    {
        free(s_framebuffer);
        s_framebuffer = NULL;
    }
    s_width  = 0;
    s_height = 0;
}
