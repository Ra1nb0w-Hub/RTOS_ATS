#ifndef EMV_INTERNAL_H
#define EMV_INTERNAL_H

#include "../include/emv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EMV_DEFAULT_CAPK_CAPACITY 8                     // CAPK容器默认容量
#define EMV_DEFAULT_CANDIDATE_APP_CAPACITY 5            // 候选应用容器默认容量
#define EMV_DEFAULT_APP_PARAMETER_CAPACITY 16           // 应用参数容器默认容量
#define EMV_DEFAULT_OFFLINE_AUTH_RECORD_CAPACITY 2      // 离线认证记录容器默认容量
#define EMV_DEFAULT_TAG_ITEMS_CAPACITY 64               // TLV容器默认容量

#define EMV_SHA1_LEN 20U                                // SHA-1输出长度
#define EMV_FLAG_HEADER 0x6AU                           // 恢复数据的头标记
#define EMV_FLAG_TRAILER 0xBCU                          // 恢复数据的尾标记
#define EMV_FLAG_FORMAT_ISSUER_PK 0x02U                 // 恢复数据的数据格式标记：发卡机构公钥
#define EMV_FLAG_FORMAT_SSAD 0x03U                      // 恢复数据的数据格式标记：SSAD
#define EMV_FLAG_FORMAT_ICC_PK 0x04U                    // 恢复数据的数据格式标记：ICC公钥
#define EMV_FLAG_FORMAT_SDAD 0x05U                      // 恢复数据的数据格式标记：SDAD
#define EMV_HASH_ALG_SHA1 0x01U                         // 恢复数据的哈希算法标记：SHA-1
#define EMV_PK_ALG_RSA 0x01U                            // 恢复数据的公钥算法标记：RSA

#define EMV_GAC_TYPE_AAC 0U                             // 生成应用密文类型：AAC
#define EMV_GAC_TYPE_TC 1U                              // 生成应用密文类型：TC
#define EMV_GAC_TYPE_ARQC 2U                            // 生成应用密文类型：ARQC

#define EmvLog(format, ...) emv_set_log("[%s][%d] " format "\r\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define EmvHexLog(title, data, len) emv_set_log_hex(title, data, len)

// 容器
typedef struct EMVContainer {
    void **items;                                       // 元素指针数组
    size_t capacity;                                    // 容量
    size_t count;                                       // 当前元素数量
} EMVContainer;

// 脱机认证记录
typedef struct EMVOfflineAuthRecord {
    unsigned char data[EMV_MAX_TAG_VALUE_LEN];          // 数据
    size_t length;                                      // 数据长度
} EMVOfflineAuthRecord;

// EMV会话信息
typedef struct EMVSession {
    EMVTerminalCapabilities terminal_capabilities;                          // 终端能力
    EMVAdditionalTerminalCapabilities additional_terminal_capabilities;     // 额外终端能力
    EMVTransactionRequest request;                                          // 交易请求
    EMVInterfaceType interface_type;                                        // 接口类型
    EMVContainer tlv;                                                       // TLV容器
    EMVContainer candidate_app;                                             // 候选应用容器
    EMVAppParameter *app_parameter;                                         // 选择的应用参数
    EMVContainer offline_auth_record;                                       // 脱机认证记录容器
    EMVAppInterchangeProfile aip;                                           // 应用交互配置
    EMVTerminalVerificationResults tvr;                                     // 终端验证结果
    EMVTransactionStatusInfo tsi;                                           // 交易状态信息
    EMVCvmResults cvmr;                                                     // CVR结果
    EMVIccPublicKeyInfo icc_pk;                                             // ICC公钥信息
} EMVSession;

extern EMVSession g_emv_session;
extern EMVReaderInterface g_emv_reader;
extern EMVTerminalConfig g_emv_terminal;

void emv_set_log(const char *format, ...);
void emv_set_log_hex(char *pcTitle, void *pvData, unsigned int uiDataLen);

int emv_tools_container_init(EMVContainer *container, size_t capacity);
int emv_tools_container_add(EMVContainer *container, void *item);
int emv_tools_container_clear(EMVContainer *container);
int emv_tools_rsa_public_key(const unsigned char *modulus, size_t modulus_len, const unsigned char *exponent, size_t exponent_len, const unsigned char *input, size_t input_len, unsigned char *output);
void emv_tools_parse_aip(const unsigned char aip[2], EMVAppInterchangeProfile *field);
void emv_tools_save_tvr(EMVTerminalVerificationResults *field);
void emv_tools_save_tsi(EMVTransactionStatusInfo *field);
void emv_tools_parse_auc(const unsigned char auc[2], EMVApplicationUsageControl *field);
void emv_tools_parse_cvm_code(const unsigned char cvm_code, EVMCvmCode *field);
void emv_tools_parse_terminal_capabilities(const unsigned char terminal_capabilities[3], EMVTerminalCapabilities *field);
void emv_tools_parse_additional_terminal_capabilities(const unsigned char additional_terminal_capabilities[5], EMVAdditionalTerminalCapabilities *field);
size_t emv_tools_hex_to_number(const unsigned char *hex, size_t hex_len);
size_t emv_tools_bcd_to_number(const unsigned char *bcd, size_t bcd_len);
int emv_tools_build_dol_data(unsigned short tag, unsigned char *data, size_t *data_len);
int emv_tools_parse_cid(const unsigned char cid, EMVCryptogramInformationData *field);

int emv_tlv_build(unsigned short usTag, unsigned char *pucValue, size_t uiLength, unsigned char *pucOutData, size_t *puiOutDataLen);
int emv_tlv_parse_tag(const unsigned char *data, size_t length, size_t *offset, uint16_t *tag_out);
int emv_tlv_parse_length(const unsigned char *data, size_t length, size_t *offset, size_t *value_length_out);
int emv_tlv_parse_data_with_save(const unsigned char *data, size_t length);
bool emv_tlv_find_tag(const unsigned char *data, size_t length, uint16_t target_tag, uint8_t depth, const unsigned char **value_ptr_out, size_t *value_length_out);

EMVCapk *emv_capk_find(const unsigned char *rid, unsigned char key_id);
EMVAppParameter * emv_app_parameter_match(const unsigned char *aid, size_t aid_len);
EMVContainer *emv_app_parameter_get_container(void);

int emv_cmd_select(unsigned char *name, size_t name_len, bool next_occurrence, unsigned char *response, size_t *response_len);
int emv_cmd_get_processing_options(unsigned char *pdol, size_t pdol_len, EMVAppFileLocator **afl, size_t *afl_count);
int emv_cmd_read_record(unsigned char record_no, unsigned char sfi, unsigned char *response, size_t *response_len);
int emv_cmd_internal_authenticate(const unsigned char *dynamic_data, size_t dynamic_data_len, unsigned char *sdad, size_t *sdad_len);
int emv_cmd_cardholder_verify(unsigned char *pinblock, size_t pin_block_len, bool plaintext);
int emv_cmd_get_challenge(unsigned char *challenge, size_t *challenge_len);
int emv_cmd_get_data(uint16_t tag, unsigned char *value, size_t *value_len);
int emv_cmd_generate_ac(unsigned char type, bool cda_request, const unsigned char *cdol, size_t cdol_len);
int emv_cmd_external_authenticate(const unsigned char *arpc, size_t arpc_len);
int emv_cmd_issuer_script(const unsigned char *script_cmd, size_t script_cmd_len);

int emv_app_selection(void);
int emv_app_init(void);
void emv_offline_auth_set_tvr(bool mark_missing_data, bool card_in_blacklist);
int emv_offline_auth_recover_issuer(const EMVCapk *capk_info, EMVIssuerPublicKeyInfo *issuer_info);
int emv_offline_auth_recover_icc(const EMVIssuerPublicKeyInfo *issuer_info, EMVIccPublicKeyInfo *icc_pk);
int emv_offline_auth_add_record(const unsigned char *data, size_t length);
int emv_offline_auth_cda(void);
int emv_offline_auth_cda_verify_sdad(void);
int emv_offline_auth_dda(void);
int emv_offline_auth_sda(void);
int emv_process_restrictions(void);
int emv_cardholder_verify(void);
int emv_risk_management(void);
int emv_action_analysis(bool *need_online);
int emv_online_complete(const char *arc, const unsigned char *tlv, size_t tlv_len);

#ifdef __cplusplus
}
#endif

#endif
