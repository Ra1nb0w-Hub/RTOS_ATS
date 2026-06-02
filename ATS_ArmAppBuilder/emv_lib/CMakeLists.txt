# ─── EMV 库 ──────────────────────────────────────────────────────────
# 构建为静态库, 供 ats_app 链接使用。

file(GLOB EMV_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c"
)

add_library(emv STATIC ${EMV_SOURCE_FILES})

target_include_directories(emv
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core"
)

target_compile_options(emv PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -std=gnu11
    -Os
    -g3
)

# EMV 模块依赖 mbedtls 的密码学原语 (RSA / SHA / 大数运算)
target_link_libraries(emv PUBLIC mbedcrypto)
