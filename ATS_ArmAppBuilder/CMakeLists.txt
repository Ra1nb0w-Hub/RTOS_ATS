cmake_minimum_required(VERSION 3.16)

project(ATS_ArmAppBuilder LANGUAGES C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED ATS_MSP_STACK_KB)
    set(ATS_MSP_STACK_KB 4)
endif()
if(NOT DEFINED ATS_RTOS_RAM_KB)
    set(ATS_RTOS_RAM_KB 256)
endif()
if(NOT DEFINED ATS_BOARD_RAM_KB)
    set(ATS_BOARD_RAM_KB 1024)
endif()
if(NOT DEFINED ATS_BOARD_ROM_KB)
    set(ATS_BOARD_ROM_KB 1024)
endif()

math(EXPR ATS_MSP_STACK_BYTES "${ATS_MSP_STACK_KB} * 1024")
math(EXPR ATS_RTOS_RAM_BYTES "${ATS_RTOS_RAM_KB} * 1024")
math(EXPR ATS_BOARD_RAM_BYTES "${ATS_BOARD_RAM_KB} * 1024")
math(EXPR ATS_BOARD_ROM_BYTES "${ATS_BOARD_ROM_KB} * 1024")

set(ATS_GENERATED_LINKER_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/ats.ld")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/ats.ld"
    "${ATS_GENERATED_LINKER_SCRIPT}"
    @ONLY
)

file(GLOB APP_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
)

file(GLOB SDK_C_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/FreeRTOS/*.c"
)

set(SDK_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/startup_mps2_an385.s"
    ${SDK_C_SOURCE_FILES}
)

# ─── mbedtls 静态库 ────────────────────────────────────────────────
# 利用 mbedtls 自带的 CMakeLists.txt 构建, 分为三个库:
# mbedcrypto (密码学原语) → mbedx509 (证书) → mbedtls (TLS)
option(USE_STATIC_MBEDTLS_LIBRARY "" ON)
option(USE_SHARED_MBEDTLS_LIBRARY "" OFF)
option(LINK_WITH_PTHREAD "" OFF)
option(LINK_WITH_TRUSTED_STORAGE "" OFF)
option(USE_PKCS11_HELPER_LIBRARY "" OFF)
option(ENABLE_ZLIB_SUPPORT "" OFF)

# ─── 全局编译选项（所有子库继承） ──────────────────────────
# 必须放在 add_subdirectory 之前，确保 mbedtls/emv/mqtt
# 都以 Cortex-M3 Thumb 模式编译，避免 UNDEFINSTR 故障。
add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -ffunction-sections
    -fdata-sections
    -fno-common
    -g3
)

set(MBEDTLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_lib/mbedtls")
set(MBEDTLS_CONFIG_FILE "mbedtls_config_ats.h")

add_subdirectory(
    "${MBEDTLS_DIR}/library"
    "${CMAKE_CURRENT_BINARY_DIR}/mbedtls_lib"
    EXCLUDE_FROM_ALL
)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/emv_lib")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/mqtt_lib")

add_executable(ats_app
    ${SDK_SOURCE_FILES}
    ${APP_SOURCE_FILES}
)

set_target_properties(ats_app PROPERTIES
    OUTPUT_NAME "ats_app"
    SUFFIX ".elf"
)

target_include_directories(ats_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/source"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/FreeRTOS"
    "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_lib"
)

target_compile_definitions(ats_app PRIVATE
    ATS=1
    ATS_RTOS_RAM_BYTES=${ATS_RTOS_RAM_BYTES}
    ATS_BOARD_RAM_BYTES=${ATS_BOARD_RAM_BYTES}
)

target_link_libraries(ats_app PRIVATE emv mbedtls mqtt)

target_compile_options(ats_app PRIVATE
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

target_link_options(ats_app PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -specs=nano.specs
    -specs=nosys.specs
    -T${ATS_GENERATED_LINKER_SCRIPT}
    -Wl,-u,_printf_float
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ats_app.map
)

add_custom_command(TARGET ats_app POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:ats_app>" "${CMAKE_CURRENT_BINARY_DIR}/ats_app.bin"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/ats_app.bin"
    COMMENT "Generating ats_app.bin"
)
