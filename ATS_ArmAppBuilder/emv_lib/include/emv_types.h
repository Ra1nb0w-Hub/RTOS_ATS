#ifndef EMV_TYPES_H
#define EMV_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMV_MAX_AID_LEN 16                             // 应用标识符最大长度
#define EMV_MAX_LABEL_LEN 32                           // 应用标签最大长度
#define EMV_MAX_PAN_LEN 20                             // PAN最大长度
#define EMV_MAX_PAN_SEQ_LEN 4                          // PAN序列最大长度
#define EMV_MAX_TLV_BUFFER_LEN 1024                    // TLV缓冲区最大长度
#define EMV_MAX_MODULUS_LEN 248                        // 模数最大长度
#define EMV_MAX_EXPONENT_LEN 3                         // 指数最大长度
#define EMV_MAX_DDOL_LEN 64                            // DDOL最大长度
#define EMV_MAX_TAG_VALUE_LEN 255                      // TLV值域最大长度

// 候选应用信息
typedef struct EMVCandidateApp EMVCandidateApp;

// 接口类型
typedef enum EMVInterfaceType {
    EMV_INTERFACE_NONE = 0,                            // 无
    EMV_INTERFACE_CONTACT = 0x01,                      // 接触式
    EMV_INTERFACE_CONTACTLESS = 0x02,                  // 非接触式
    EMV_INTERFACE_MAGAGNETIC = 0x04                    // 磁条卡
} EMVInterfaceType;

// 交易类型
typedef enum EMVTransactionType {
    EMV_TRANS_PURCHASE = 0x00,                         // 购买、商品
    EMV_TRANS_REFUND = 0x20,                           // 退货、退款
    EMV_TRANS_INQUIRY = 0x31,                          // 查余额
} EMVTransactionType;

/**
 * @brief 日志回调函数。
 * 
 * @param pcMessage 日志消息指针。
 */
typedef void (*EMVLogCallback)(const char *pcMessage);

/**
 * @brief 随机数回调函数。
 * 
 * @param pucOutData 输出缓冲区指针。
 * @param uiNeedLen 需要的随机数长度。
 */
typedef void (*EMVRandomCallback)(unsigned char *pucOutData, unsigned int uiNeedLen);

/**
 * @brief 检测卡片回调函数。
 * 
 * @param eInterfaceType 接口类型。
 * @return EMV_OK 表示成功，否则返回错误码。
 */
typedef int (*EMVDetectCardCallback)(EMVInterfaceType eInterfaceType);

/**
 * @brief 选择应用回调函数。
 * 
 * @param pstApps 候选应用指针数组。
 * @param pstApps 候选应用数量。
 * @param puiSelectedIndex 选中的应用索引指针。
 * @return EMV_OK 表示成功，否则返回错误码。
 */
typedef int (*EMVSelectAppCallback)(const EMVCandidateApp *pstApps, unsigned int uiAppCount, unsigned int *puiSelectedIndex);

/**
 * @brief 输入PIN回调函数。
 * 
 * @param bIsOnlinePin 是否是联机PIN。
 * @param uiPinRetryTimes 脱机PIN重试次数。
 * @param pucOutPinBlock 输出PIN Block缓冲区指针。
 * @param uiPinBlockLen PIN Block缓冲区长度。
 * @return EMV_OK 表示成功，否则返回错误码。
 */
typedef int (*EMVInputPinCallback)(unsigned char bIsOnlinePin, unsigned int uiPinRetryTimes, unsigned char *pucOutPinBlock, unsigned int uiPinBlockLen);

// 读卡器接口
typedef struct EMVReaderInterface {
    int (*open)(void);
    int (*close)(void);
    bool (*get_status)(void);
    int (*poll_card)(EMVInterfaceType *card_interface, unsigned int timeout_ms);
    int (*cancel_io)(void);

    int (*icc_power_on)(unsigned char *atr, size_t *atr_len);
    int (*icc_power_off)(void);
    int (*icc_transceive_apdu)(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

    int (*picc_activate)(unsigned char *ats, size_t *ats_len);
    int (*picc_deactivate)(void);
    int (*picc_transceive_apdu)(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

    int (*get_last_hw_error)(void);
} EMVReaderInterface;

// 终端配置
typedef struct EMVTerminalConfig {
    unsigned char country_code[2];                     // 国家代码
    unsigned char currency_code[2];                    // 货币代码
    unsigned char currency_exponent;                   // 货币指数
    unsigned char merchant_category_code[2];           // 商户类别代码
    unsigned char terminal_type;                       // 终端类型
    unsigned char terminal_capabilities[3];            // 终端能力
    unsigned char additional_terminal_capabilities[5]; // 额外终端能力
    unsigned char floor_limit[6];                      // 终端最低交易限额
    bool support_offline;                              // 支持离线交易
    bool support_online;                               // 支持在线交易
    bool support_signature;                            // 支持签名
    bool support_contact;                              // 支持接触式卡片
    bool support_contactless;                          // 支持非接触式卡片

    EMVLogCallback log_callback;                       // 日志回调函数
    EMVRandomCallback random_callback;                 // 随机数回调函数
    EMVDetectCardCallback detect_card_callback;        // 检测卡片回调函数
    EMVSelectAppCallback select_app_callback;          // 选择应用回调函数
    EMVInputPinCallback input_pin_callback;            // 输入PIN回调函数
} EMVTerminalConfig;

// 终端能力
typedef struct EMVTerminalCapabilities {
    struct {
        unsigned char manual_input: 1;                 // 手工键盘输入
        unsigned char magnetic_stripe: 1;              // 磁条输入
        unsigned char contact_ic: 1;                   // 接触式IC卡输入
        unsigned char bit4: 1;                         // bit4: 保留位
        unsigned char bit3: 1;                         // bit3: 保留位
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } card_data_input;

    struct {
        unsigned char offline_plaintext_pin: 1;        // 离线明文PIN验证
        unsigned char online_encrypted_pin: 1;         // 联机加密PIN验证
        unsigned char paper_signature: 1;              // 纸质签名验证
        unsigned char offline_encrypted_pin: 1;        // 离线加密PIN验证
        unsigned char no_cvm: 1;                       // 无需CVM
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } cvm;

    struct {
        unsigned char sda: 1;                          // 静态数据认证(SDA)
        unsigned char dda: 1;                          // 动态数据认证(DDA)
        unsigned char capture: 1;                      // 吞卡
        unsigned char bit4: 1;                         // bit4: 保留位
        unsigned char cda: 1;                          // 复合动态数据认证(CDA)
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } security;
} EMVTerminalCapabilities;

// 额外终端能力
typedef struct EMVAdditionalTerminalCapabilities {
    struct {
        unsigned char cash: 1;                         // 现金
        unsigned char goods: 1;                        // 商品
        unsigned char services: 1;                     // 服务
        unsigned char cash_back: 1;                    // 返现
        unsigned char inquiry: 1;                      // 查询
        unsigned char transfer: 1;                     // 转账
        unsigned char payment: 1;                      // 付款
        unsigned char administrative: 1;               // 管理
    } trans_type1;

    struct {
        unsigned char cash_deposit: 1;                 // 存款
        unsigned char bit6: 1;                         // bit6: 保留位
        unsigned char bit5: 1;                         // bit5: 保留位
        unsigned char bit4: 1;                         // bit4: 保留位
        unsigned char bit3: 1;                         // bit3: 保留位
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } trans_type2;

    struct {
        unsigned char number_keys: 1;                  // 数字键
        unsigned char alpha_special_keys: 1;           // 字母和特殊字符键
        unsigned char command_keys: 1;                 // 命令键
        unsigned char funtion_keys: 1;                 // 功能键
        unsigned char bit3: 1;                         // bit3: 保留位
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } terminal_data_in;

    struct {
        unsigned char print_to_attendant: 1;           // 打印给服务员
        unsigned char print_to_cardholder: 1;          // 打印给持卡人
        unsigned char display_to_attendant: 1;         // 展示给服务员
        unsigned char display_to_cardholder: 1;        // 展示给持卡人
        unsigned char bit3: 1;                         // bit3: 保留位
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char code_table_10: 1;                // 编码表10
        unsigned char code_table_9: 1;                 // 编码表9
    } terminal_data_out1;

    struct {
        unsigned char code_table_8: 1;                 // 编码表8
        unsigned char code_table_7: 1;                 // 编码表7
        unsigned char code_table_6: 1;                 // 编码表6
        unsigned char code_table_5: 1;                 // 编码表5
        unsigned char code_table_4: 1;                 // 编码表4
        unsigned char code_table_3: 1;                 // 编码表3
        unsigned char code_table_2: 1;                 // 编码表2
        unsigned char code_table_1: 1;                 // 编码表1
    } terminal_data_out2;
} EMVAdditionalTerminalCapabilities;

// 交易请求
typedef struct EMVTransactionRequest {
    unsigned char amount[6];                           // 金额
    unsigned char amount_other[6];                     // 其他金额
    unsigned char type;                                // 交易类型
    unsigned char date[3];                             // 交易日期
    unsigned char time[3];                             // 交易时间
    uint32_t allow_interface_type;                     // 允许的接口类型
    uint32_t check_interface_timeout;                  // 检测接口超时时间，单位毫秒
    bool bForceOnline;                                 // 强制联机交易
} EMVTransactionRequest;

// 应用参数
typedef struct EMVAppParameter {
    unsigned char aid[EMV_MAX_AID_LEN];                // AID
    unsigned char aid_len;                             // AID长度
    unsigned char asi;                                 // 应用选择指示符(0:允许部分匹配 1:允许完全匹配)
    unsigned char app_version[2];                      // 应用版本号
    unsigned char tac_default[5];                      // TAC-缺省
    unsigned char tac_online[5];                       // TAC-联机
    unsigned char tac_denial[5];                       // TAC-拒绝
    unsigned char floor_limit[4];                      // 终端最低限额
    unsigned char ddol[EMV_MAX_DDOL_LEN];              // DDOL缺省值
    unsigned char ddol_len;                            // DDOL缺省值长度
    unsigned char threshold[4];                        // 偏置随机选择的阈值
    unsigned char max_target_percentage;               // 偏置随机选择的最大目标百分数
    unsigned char target_percentage;                   // 随机选择的目标百分数
    unsigned char online_pin_flag;                     // 终端联机PIN支持能力
    unsigned char cl_floor_limit[6];                   // 非接触读写器脱机最低限额
    unsigned char cl_trans_limit[6];                   // 非接触读写器交易限额
    unsigned char cl_cvm_limit[6];                     // 非接触读写器执行CVM限额
} EMVAppParameter;

// CA认证中心公钥
typedef struct EMVCapk {
    unsigned char rid[5];                              // RID
    unsigned char key_id;                              // Key Index
    unsigned char modulus[EMV_MAX_MODULUS_LEN];        // 模数
    unsigned char modulus_len;                         // 模数长度
    unsigned char exponent[EMV_MAX_EXPONENT_LEN];      // 指数
    unsigned char exponent_len;                        // 指数长度
} EMVCapk;

// 发卡行公钥信息、ICC公钥信息
typedef struct EMVPublicKeyInfo {
    unsigned char cert[EMV_MAX_TAG_VALUE_LEN];         // 证书
    size_t cert_len;                                   // 证书长度
    unsigned char remainder[EMV_MAX_TAG_VALUE_LEN];    // 证书余数
    size_t remainder_len;                              // 证书余数长度
    unsigned char exponent[EMV_MAX_EXPONENT_LEN];      // 指数
    size_t exponent_len;                               // 指数长度
    unsigned char modulus[EMV_MAX_MODULUS_LEN];        // 模数
    size_t modulus_len;                                // 模数长度
} EMVIssuerPublicKeyInfo, EMVIccPublicKeyInfo;

// 候选应用信息
typedef struct EMVCandidateApp {
    EMVAppParameter *app_parameter;                    // 应用参数
    unsigned char aid[EMV_MAX_AID_LEN];                // AID
    size_t aid_len;                                    // AID长度
    char label[EMV_MAX_LABEL_LEN];                     // 应用标签
    size_t label_len;                                  // 应用标签长度
    unsigned char priority_indicator;                  // 应用优先指示器
} EMVCandidateApp;

// 应用交互特征
typedef struct EMVAppInterchangeProfile {
    unsigned char bit7: 1;                             // bit7: 保留位
    unsigned char sda: 1;                              // bit6: 支持静态数据认证(SDA)
    unsigned char dda: 1;                              // bit5: 支持动态数据认证(DDA)
    unsigned char card_holder: 1;                      // bit4: 支持持卡人认证
    unsigned char exec_risk: 1;                        // bit3: 执行终端风险管理
    unsigned char card_issuer: 1;                      // bit2: 支持发卡行认证
    unsigned char bit1: 1;                             // bit1: 保留位
    unsigned char cda: 1;                              // bit0: 支持复合数据认证(CDA)

    unsigned char byte2;                               // 第2字节: 保留
} EMVAppInterchangeProfile;

// 应用文件定位器
typedef struct EMVAppFileLocator {
    unsigned char sfi;                                 // 短文件标识符
    unsigned char first_record_no;                     // 起始的记录号
    unsigned char last_record_no;                      // 终止的记录号
    unsigned char offline_record_count;                // 用于脱机数据认证记录数量(0表示没有用于脱机认证的数据)
} EMVAppFileLocator;

// 终端验证结果
typedef struct EMVTerminalVerificationResults {
    // 数据认证结果
    struct {
        unsigned char not_executed: 1;                 // bit7: 未执行脱机数据认证
        unsigned char sda_failed: 1;                   // bit6: 静态数据认证失败
        unsigned char card_data_missing: 1;            // bit5: 卡片数据缺失
        unsigned char card_in_blacklist: 1;            // bit4: 卡片出现在终端例外文件(黑名单)中
        unsigned char dda_failed: 1;                   // bit3: 动态数据认证失败
        unsigned char cda_failed: 1;                   // bit2: 复合数据认证/应用密文生成失败
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } data_verify_result;

    // 处理限制结果
    struct {
        unsigned char version_inconformity: 1;         // bit7: IC卡和终端应用版本不一致
        unsigned char app_expired: 1;                  // bit6: 应用已过期
        unsigned char app_effective: 1;                // bit5: 应用尚未生效
        unsigned char unallowed_service: 1;            // bit4: 卡片不允许请求的服务
        unsigned char new_card: 1;                     // bit3: 新卡
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } limit_result;
    
    // 持卡人认证结果
    struct {
        unsigned char cardholder_failed: 1;            // bit7: 持卡人验证失败
        unsigned char unknown_cvm: 1;                  // bit6: 未知的CVM
        unsigned char pin_retry_limit: 1;              // bit5: PIN重试次数超限
        unsigned char pinpad_failed: 1;                // bit4: 要求输入PIN，但密码键盘不存在或工作不正常
        unsigned char not_input_pin: 1;                // bit3: 要求输入PIN，密码键盘存在，但未输入PIN
        unsigned char input_online_pin: 1;             // bit2: 输入联机PIN
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } cardholder_verify_result;

    // 风险管理结果
    struct {
        unsigned char amount_lower_limit: 1;           // bit7: 交易超过最低限额
        unsigned char exceed_offline_lower_limit: 1;   // bit6: 超过连续脱机交易下限
        unsigned char exceed_offline_upper_limit: 1;   // bit5: 超过连续脱机交易上限
        unsigned char random_online: 1;                // bit4: 交易被随机选择联机处理
        unsigned char force_online: 1;                 // bit3: 商户要求联机交易
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } risk_result;

    // 行为分析结果
    struct {
        unsigned char use_default_tdol: 1;             // bit7: 使用缺省TDOL
        unsigned char icc_verify_failed: 1;            // bit6: 发卡行认证失败
        unsigned char gac_before_failed: 1;            // bit5: 最后一次GENERATE AC命令之前脚本处理失败
        unsigned char gac_after_failed: 1;             // bit4: 最后一次GENERATE AC命令之后脚本处理失败
        unsigned char bit3: 1;                         // bit3: 保留位
        unsigned char bit2: 1;                         // bit2: 保留位
        unsigned char bit1: 1;                         // bit1: 保留位
        unsigned char bit0: 1;                         // bit0: 保留位
    } action_analysis_result;
} EMVTerminalVerificationResults;

// 交易状态信息
typedef struct EMVTransactionStatusInfo {
    unsigned char offline_authenticate: 1;             // bit7: 脱机数据认证已执行
    unsigned char cardholder_verification: 1;          // bit6: 持卡人认证已执行
    unsigned char card_risk_management: 1;             // bit5: 卡片风险管理已执行
    unsigned char issuing_bank_verification: 1;        // bit4: 发卡行认证已执行
    unsigned char terminal_risk_management: 1;         // bit3: 终端风险管理已执行
    unsigned char issuing_bank_script: 1;              // bit2: 发卡行脚本处理已执行
    unsigned char byte1_bit1: 1;                       // bit1: 保留位
    unsigned char byte1_bit0: 1;                       // bit0: 保留位

    unsigned char byte2;                               // 第2字节: 保留
} EMVTransactionStatusInfo;

// 应用用途控制(AUC)
typedef struct EMVApplicationUsageControl {
    unsigned char domestic_cash: 1;                    // bit7: 国内现金交易有效
    unsigned char international_cash: 1;               // bit6: 国外现金交易有效
    unsigned char domestic_goods: 1;                   // bit5: 国内商品交易有效
    unsigned char international_goods: 1;              // bit4: 国外商品交易有效
    unsigned char domestic_service: 1;                 // bit3: 国内服务交易有效
    unsigned char international_service: 1;            // bit2: 国外服务交易有效
    unsigned char atm: 1;                              // bit1: ATM终端有效
    unsigned char except_atm: 1;                       // bit0: 除ATM外的终端有效

    unsigned char domestic_cash_back: 1;               // bit7: 允许国内返现
    unsigned char international_cash_back: 1;          // bit6: 允许国外返现
    unsigned char byte2_bit5: 1;                       // bit5: 保留位
    unsigned char byte2_bit4: 1;                       // bit4: 保留位
    unsigned char byte2_bit3: 1;                       // bit3: 保留位
    unsigned char byte2_bit2: 1;                       // bit2: 保留位
    unsigned char byte2_bit1: 1;                       // bit1: 保留位
    unsigned char byte2_bit0: 1;                       // bit0: 保留位
} EMVApplicationUsageControl;

// CVM代码
typedef struct EVMCvmCode {
    unsigned char custom: 1;                           // 只有符合此规范的取值(如果不为1, 说明有自定义的值)
    unsigned char rule: 1;                             // 规则: 0-如果此CVM失败, 则持卡人验证失败 1-如果此CVM失败, 应用后续的规则
    unsigned char type: 6;                             // 类型
} EVMCvmCode;

// 持卡人验证方法(CVM)
typedef struct EMVCvm {
    unsigned char code;                                // CVM代码
    unsigned char condition_code;                      // CVM条件码
} EMVCvm;

// 持卡人验证方法(CVM)结果
typedef struct EMVCvmResults {
    unsigned char code;                                // CVM代码
    unsigned char condition_code;                      // CVM条件码
    unsigned char result;                              // CVM执行结果 0-未知 1-失败 2-成功
} EMVCvmResults;

// 密文信息数据(CID)
typedef struct EMVCryptogramInformationData {
    unsigned char type: 2;                             // 密文类型 0-AAC 1-TC 2-ARQC
    unsigned char bit6: 1;                             // bit6: 保留位
    unsigned char bit5: 1;                             // bit5: 保留位
    unsigned char exist_code : 1;                      // 是否存在建议代码
    unsigned char code: 3;                             // 建议代码 0-未提供 1-服务不允许 2-PIN尝试次数已超出限制 3-发卡行认证失败 
} EMVCryptogramInformationData;

#ifdef __cplusplus
}
#endif

#endif
