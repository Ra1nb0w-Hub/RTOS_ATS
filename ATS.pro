# ─── ATS 项目顶层 ────────────────────────────────────────────────────────────────
# 子项目模式：mbedtls 和 mqtt_embed 各自编译为静态库，ATS app 链接这两个库
# 库源码不变时不会重新编译，只有源码变化才会触发重编

TEMPLATE = subdirs

SUBDIRS += \
    mbedtls_lib \
    mqtt_lib \
    lua_lib \
    emv_lib \
    ATS_app

# ATS_app 依赖三个库，确保先编库再编 app
ATS_app.depends = mbedtls_lib mqtt_lib lua_lib emv_lib
