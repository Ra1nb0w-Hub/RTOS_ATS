TEMPLATE = lib
CONFIG += staticlib c11
CONFIG -= qt
TARGET = ats_emv

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/../mbedtls_lib/mbedtls/include

SOURCES += \
    core/emv_action_analysis.c \
    core/emv_api.c \
    core/emv_app_capk.c \
    core/emv_app_init.c \
    core/emv_app_parameter.c \
    core/emv_app_selection.c \
    core/emv_cardholder_verify.c \
    core/emv_cmd.c \
    core/emv_offline_auth_cda.c \
    core/emv_offline_auth_dda.c \
    core/emv_offline_auth_sda.c \
    core/emv_offline_auth.c \
    core/emv_online_complete.c \
    core/emv_process_restrictions.c \
    core/emv_risk_management.c \
    core/emv_tlv.c \
    core/emv_tools.c \
    port/emv_reader_pcsc.c

HEADERS += \
    include/emv_api.h \
    include/emv_types.h \
    include/emv_error.h \
    include/emv_tags.h \
    core/emv_internal.h \
    port/emv_reader_if.h \
    port/emv_reader_pcsc.h

win32: LIBS += -lwinscard
