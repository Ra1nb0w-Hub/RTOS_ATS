#ifndef EMV_ERROR_H
#define EMV_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMV_OK (0)                                      // 成功
#define EMV_ERR_INVALID_PARAM (-1000)                   // 无效参数
#define EMV_ERR_NOT_INITIALIZED (-1001)                 // 未初始化
#define EMV_ERR_NO_MEMORY (-1002)                       // 内存不足
#define EMV_ERR_BAD_STATE (-1003)                       // 状态错误
#define EMV_ERR_BUFFER_TOO_SMALL (-1004)                // 缓冲区太小
#define EMV_ERR_NOT_FOUND (-1005)                       // 未找到数据、标签
#define EMV_ERR_NOT_SUPPORTED (-1006)                   // 不支持的操作
#define EMV_ERR_BAD_DATA (-1007)                        // 无效数据
#define EMV_ERR_BAD_RESPONSE (-1008)                    // 无效响应
#define EMV_ERR_TIMEOUT (-1009)                         // 超时退出
#define EMV_ERR_CANCEL (-1010)                          // 取消操作
#define EMV_ERR_NOT_INPUT (-1011)                       // 未输入数据
#define EMV_ERR_HASH_VERIFY (-1012)                     // Hash验证失败
#define EMV_ERR_ICC_KEY_INVALID (-1013)                 // ICC密钥无效
#define EMV_ERR_CVM_FAILED (-1014)                      // CVM验证失败
#define EMV_ERR_TLV_TAG_INVALID (-1015)                 // 无效的TLV标签
#define EMV_ERR_TLV_LENGTH_INVALID (-1016)              // 无效的TLV长度
#define EMV_ERR_CARD_DENIED (-1017)                     // 卡片拒绝本次交易

#define EMV_ERR_READER_OPEN (-1100)                     // 读卡器打开失败
#define EMV_ERR_READER_POLL (-1101)                     // 读卡器轮询失败
#define EMV_ERR_READER_IO (-1102)                       // 读卡器IO失败
#define EMV_ERR_READER_TIMEOUT (-1103)                  // 读卡器超时
#define EMV_ERR_READER_NOT_FOUND_CARD (-1104)           // 读卡器未找到卡片

#define EMV_ERR_MBEDTLS_RSA_IMPORT (-1200)              // RSA导入密钥失败
#define EMV_ERR_MBEDTLS_RSA_COMPLETE (-1201)            // RSA完成密钥上下文失败
#define EMV_ERR_MBEDTLS_RSA_CHECK_PUBKEY (-1202)        // RSA检查公钥失败
#define EMV_ERR_MBEDTLS_RSA_PUBKEY_DECRYPT (-1203)      // RSA公钥解密失败
#define EMV_ERR_MBEDTLS_SHA1_STARTS (-1204)             // Sha1开始失败
#define EMV_ERR_MBEDTLS_SHA1_UPDATE (-1205)             // Sha1更新失败
#define EMV_ERR_MBEDTLS_SHA1_FINISH (-1206)             // Sha1完成失败


#ifdef __cplusplus
}
#endif

#endif
