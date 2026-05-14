#ifndef EMV_API_H
#define EMV_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "emv_types.h"
#include "emv_error.h"
#include "emv_tags.h"

// 设置终端配置
int emv_terminal_set_config(const EMVTerminalConfig *config);
int emv_terminal_get_config(EMVTerminalConfig *config);

// 清空应用参数
void emv_app_parameter_clear(void);
// 导入应用参数
int emv_app_parameter_import(const EMVAppParameter *new_app_parameter);
// 从TLV数据导入应用参数
int emv_app_parameter_import_from_tlv(const unsigned char *tlv_data, unsigned int tlv_length);

// 清空认证中心公钥
void emv_capk_clear(void);
// 导入认证中心公钥
int emv_capk_import(const EMVCapk *new_capk);
// 从TLV数据导入认证中心公钥
int emv_capk_import_from_tlv(const unsigned char *tlv_data, unsigned int tlv_length);

// 清空终端和卡片的TLV数据
void emv_tlv_clear(void);
// 设置指定Tag的数据
int emv_tlv_set(uint16_t tag, const unsigned char *value, size_t length);
// 获取指定Tag的数据
int emv_tlv_get(uint16_t tag, unsigned char *value, size_t *length);
// 检查指定Tag的数据是否存在
bool emv_tlv_exists(uint16_t tag);

// 开始交易
int emv_step_transaction_begin(const EMVTransactionRequest *request);
// 交易处理
int emv_step_transaction_process(bool *need_online);
// 交易联机完成
int emv_step_transaction_online_complete(unsigned char *arc, unsigned char *arpc, unsigned int arpc_len, unsigned char *script, unsigned int script_len);
// 结束交易
int emv_step_transaction_end(void);

#ifdef __cplusplus
}
#endif

#endif
