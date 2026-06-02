/**
 * \file mbedtls_config_ats.h
 * \brief 裸机 Cortex-M3 嵌入式 mbedtls 裁剪配置
 *
 * 引入默认配置后, 关闭依赖操作系统 (socket / 文件系统 / 时间) 的模块。
 */

#ifndef MBEDTLS_CONFIG_ATS_H
#define MBEDTLS_CONFIG_ATS_H

/* 先引入 mbedtls 默认配置 */
#include "mbedtls/config.h"

/* ─── 禁用平台熵源 (无 /dev/urandom 或 Windows CryptoAPI) ──────── */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

/* ─── 禁用依赖 OS 的模块 ────────────────────────────────────────── */
#undef MBEDTLS_NET_C              /* BSD socket */
#undef MBEDTLS_TIMING_C           /* OS 时间函数 */
#undef MBEDTLS_HAVEGE_C           /* 依赖 TIMING_C */
#undef MBEDTLS_FS_IO              /* 文件系统 */

/* ─── 禁用线程 (FreeRTOS 环境下可按需开启) ──────────────────────── */
#if defined(MBEDTLS_THREADING_C)
#undef MBEDTLS_THREADING_C
#endif

#endif /* MBEDTLS_CONFIG_ATS_H */
