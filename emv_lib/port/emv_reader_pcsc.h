#ifndef EMV_READER_PCSC_H
#define EMV_READER_PCSC_H

#include "emv_reader_if.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 使用 PC/SC 实现初始化读卡器接口。
 *
 * @param reader_if 待读写的读卡器接口对象。
 *
 * @return EMV_OK 表示成功，否则返回错误码。
 */
int emv_reader_use_pcsc_driver(EMVReaderInterface *reader_if);

#ifdef __cplusplus
}
#endif

#endif
