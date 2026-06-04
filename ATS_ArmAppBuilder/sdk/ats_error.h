#pragma once

#define ATS_EC_OK                       (0)       // 成功
#define ATS_EC_INVALID_PARAM            (-1)      // 无效参数
#define ATS_EC_TIMEOUT                  (-2)      // 超时
#define ATS_EC_BAD_DATA                 (-3)      // 数据错误、数据格式错误
#define ATS_EC_TOO_LARGE                (-4)      // 数据过大，缓冲区不足
#define ATS_EC_NO_MEMORY                (-5)      // 内存不足(申请内存失败)
#define ATS_EC_RPC_NOT_INIT             (-6)      // RPC未初始化
