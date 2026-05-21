#ifndef EMV_TAGS_H
#define EMV_TAGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define EMV_TAG_AID                                     (0x4FU) // 应用标识符(AID)
#define EMV_TAG_PAN                                     (0x5AU) // 主卡号(PAN)
#define EMV_TAG_APPLICATION_LABEL                       (0x50U) // 应用标签
#define EMV_TAG_TRACK2_EQUIVALENT                       (0x57U) // 磁道2等效数据
#define EMV_TAG_APPLICATION_EXPIRATION_DATE             (0x5F24U) // 应用过期日期
#define EMV_TAG_APPLICATION_EFFECTIVE_DATE              (0x5F25U) // 应用生效日期
#define EMV_TAG_ISSUER_COUNTRY_CODE                     (0x5F28U) // 发卡国家代码
#define EMV_TAG_TRANSACTION_CURRYENCY_CODE              (0x5F2AU) // 交易货币代码
#define EMV_TAG_PAN_SEQUENCE_NUMBER                     (0x5F34U) // 主卡号序列号
#define EMV_TAG_APPLICATION_TEMPLATE                    (0x61U) // 应用模板
#define EMV_TAG_FCI_TEMPLATE                            (0x6FU) // 文件控制信息(FCI)模板
#define EMV_TAG_RECORD_TEMPLATE                         (0x70U) // 记录模板
#define EMV_TAG_ISSUER_SCRIPT_TEMPLATE_1                (0x71U) // 发卡行脚本模板1
#define EMV_TAG_ISSUER_SCRIPT_TEMPLATE_2                (0x72U) // 发卡行脚本模板2
#define EMV_TAG_RESPONSE_TEMPLATE2                      (0x77U) // 响应报文模板2
#define EMV_TAG_RESPONSE_TEMPLATE1                      (0x80U) // 响应报文模板1
#define EMV_TAG_AIP                                     (0x82U) // 应用交互特征(AIP)
#define EMV_TAG_DF_NAME                                 (0x84U) // 专用文件(DF)名称
#define EMV_TAG_ISSUER_SCRIPT_COMMAND                   (0x86U) // 发卡行脚本命令
#define EMV_TAG_APPLICATION_PRIORITY_INDICATOR          (0x87U) // 应用优先级指示器
#define EMV_TAG_SFI                                     (0x88U) // 短文件标识符(SFI)
#define EMV_TAG_AUTH_RESPONSE_CODE                      (0x8AU) // 授权响应码(ARC)
#define EMV_TAG_CDOL1                                   (0x8CU) // 卡片风险管理数据对象列表1(CDOL1)
#define EMV_TAG_CDOL2                                   (0x8DU) // 卡片风险管理数据对象列表2(CDOL2)
#define EMV_TAG_CVM_LIST                                (0x8EU) // 持卡人验证方法(CVM)列表
#define EMV_TAG_CA_PKI                                  (0x8FU) // CA公钥索引(PKI)
#define EMV_TAG_ISSUER_PUBLIC_KEY_CERTIFICATE           (0x90U) // 发卡行公钥证书
#define EMV_TAG_ISSUER_AUTH_DATA                        (0x91U) // 发卡行认证数据(ARPC)
#define EMV_TAG_ISSUER_PUBLIC_KEY_REMAINDER             (0x92U) // 发卡行公钥余数
#define EMV_TAG_SSAD                                    (0x93U) // 签名的静态应用数据(SSAD)
#define EMV_TAG_AFL                                     (0x94U) // 应用文件定位器(AFL)
#define EMV_TAG_TVR                                     (0x95U) // 终端验证结果(TVR)
#define EMV_TAG_TSI                                     (0x9BU) // 交易状态信息(TSI)
#define EMV_TAG_TRANSACTION_DATE                        (0x9AU) // 交易日期
#define EMV_TAG_TRANSACTION_TYPE                        (0x9CU) // 交易类型
#define EMV_TAG_AMOUNT                                  (0x9F02U) // 授权金额
#define EMV_TAG_AMOUNT_OTHER                            (0x9F03U) // 其他金额
#define EMV_TAG_AUC                                     (0x9F07U) // 应用用途控制(AUC)
#define EMV_TAG_APPLICATION_VERSION_NUMBER_CARD         (0x9F08U) // 卡片应用版本号
#define EMV_TAG_APPLICATION_VERSION_NUMBER_TERMINAL     (0x9F09U) // 终端应用版本号
#define EMV_TAG_IAC_DEFAULT                             (0x9F0DU) // 发卡行行为代码(IAC)-缺省
#define EMV_TAG_IAC_DENIAL                              (0x9F0EU) // 发卡行行为代码(IAC)-拒绝
#define EMV_TAG_IAC_ONLINE                              (0x9F0FU) // 发卡行行为代码(IAC)-联机
#define EMV_TAG_IAD                                     (0x9F10U) // 发卡行应用数据(IAD)
#define EMV_TAG_LAST_ONLINE_ATC_REGISTER                (0x9F13U) // 上次联机应用交易计数器(ATC)寄存器
#define EMV_TAG_LOWER_CONSECUTIVE_OFFLINE_LIMIT         (0x9F14U) // 连续脱机交易下限
#define EMV_TAG_PIN_TRY_COUNT                           (0x9F17U) // PIN尝试次数器
#define EMV_TAG_TERMINAL_COUNTRY_CODE                   (0x9F1AU) // 终端国家代码
#define EMV_TAG_TERMINAL_FLOOR_LIMIT                    (0x9F1BU) // 终端最低交易限额
#define EMV_TAG_TRANSACTION_TIME                        (0x9F21U) // 交易时间
#define EMV_TAG_UPPER_CONSECUTIVE_OFFLINE_LIMIT         (0x9F23U) // 连续脱机交易上限
#define EMV_TAG_AC                                      (0x9F26U) // 应用密文(AC)
#define EMV_TAG_CID                                     (0x9F27U) // 密文信息数据(CID)
#define EMV_TAG_ISSUER_PUBLIC_KEY_EXPONENT              (0x9F32U) // 发卡行公钥指数
#define EMV_TAG_TERMINAL_CAPABILITIES                   (0x9F33U) // 终端能力
#define EMV_TAG_CVM_RESULTS                             (0x9F34U) // 持卡人验证方法(CVM)结果
#define EMV_TAG_TERMINAL_TYPE                           (0x9F35U) // 终端类型
#define EMV_TAG_ATC                                     (0x9F36U) // 应用交易计数器(ATC)
#define EMV_TAG_UNPREDICTABLE_NUMBER                    (0x9F37U) // 不可预知数
#define EMV_TAG_PDOL                                    (0x9F38U) // 处理选项数据对象列表(PDOL)
#define EMV_TAG_ADDITIONAL_TERMINAL_CAPABILITIES        (0x9F40U) // 额外终端能力
#define EMV_TAG_APPLICATION_CURRENCY_CODE               (0x9F42U) // 应用货币代码
#define EMV_TAG_DATA_AUTH_CODE                          (0x9F45U) // 数据认证码
#define EMV_TAG_ICC_PUBLIC_KEY_CERTIFICATE              (0x9F46U) // IC卡公钥证书
#define EMV_TAG_ICC_PUBLIC_KEY_EXPONENT                 (0x9F47U) // IC卡公钥指数
#define EMV_TAG_ICC_PUBLIC_KEY_REMAINDER                (0x9F48U) // IC卡公钥余数
#define EMV_TAG_DDOL                                    (0x9F49U) // 动态数据认证数据对象列表(DDOL)
#define EMV_TAG_SDA_DATA_TAG_LIST                       (0x9F4AU) // 静态数据认证标签列表
#define EMV_TAG_SDAD                                    (0x9F4BU) // 签名的动态应用数据(SDAD)
#define EMV_TAG_ICC_DYNAMIC_NUMBER                      (0x9F4CU) // ICC动态数
#define EMV_TAG_MERCHANT_CUSTOM_DATA                    (0x9F7CU) // 商户自定义数据
#define EMV_TAG_FCI_DATA                                (0xA5U) // FCI专用模板

#ifdef __cplusplus
}
#endif

#endif
