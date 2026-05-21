cmake_minimum_required(VERSION 3.16)

project(ATS_ArmAppBuilder LANGUAGES C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ATS_BOOT_BRAM_KB 4096)
set(ATS_CPU_CLOCK_HZ 25000000)

set(ATS_BOARD_RAM_KB 4096 CACHE STRING "MPS2 AN385 SRAM working RAM budget in KB")
set(ATS_DEMO_HEAP_KB 4096 CACHE STRING "Requested demo heap size in KB")
set(ATS_ISR_STACK_KB 4 CACHE STRING "Requested ISR stack reservation in KB")
set(ATS_DEMO_MAIN_STACK_KB 256 CACHE STRING "Requested main stack size in KB")

function(ats_require_non_negative_integer INPUT_VALUE INPUT_NAME OUTPUT_NAME)
    if("${INPUT_VALUE}" STREQUAL "")
        message(FATAL_ERROR "${INPUT_NAME} must not be empty.")
    endif()

    if(NOT "${INPUT_VALUE}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${INPUT_NAME} must be a non-negative integer, got `${INPUT_VALUE}`.")
    endif()

    set(${OUTPUT_NAME} "${INPUT_VALUE}" PARENT_SCOPE)
endfunction()

function(ats_clamp_integer INPUT_VALUE MIN_VALUE MAX_VALUE OUTPUT_NAME)
    set(_clamped "${INPUT_VALUE}")
    if(_clamped LESS MIN_VALUE)
        set(_clamped "${MIN_VALUE}")
    endif()
    if(_clamped GREATER MAX_VALUE)
        set(_clamped "${MAX_VALUE}")
    endif()
    set(${OUTPUT_NAME} "${_clamped}" PARENT_SCOPE)
endfunction()

ats_require_non_negative_integer("${ATS_BOARD_RAM_KB}" "ATS_BOARD_RAM_KB" ATS_BOARD_RAM_KB_VALUE)
ats_require_non_negative_integer("${ATS_DEMO_HEAP_KB}" "ATS_DEMO_HEAP_KB" ATS_DEMO_HEAP_KB_VALUE)
ats_require_non_negative_integer("${ATS_ISR_STACK_KB}" "ATS_ISR_STACK_KB" ATS_ISR_STACK_KB_VALUE)
ats_require_non_negative_integer("${ATS_DEMO_MAIN_STACK_KB}" "ATS_DEMO_MAIN_STACK_KB" ATS_DEMO_MAIN_STACK_KB_VALUE)

ats_clamp_integer("${ATS_BOARD_RAM_KB_VALUE}" 1024 4096 ATS_EFFECTIVE_BOARD_RAM_KB)
ats_clamp_integer("${ATS_DEMO_HEAP_KB_VALUE}" 128 2048 ATS_DEMO_EFFECTIVE_HEAP_KB)
ats_clamp_integer("${ATS_ISR_STACK_KB_VALUE}" 1 16 ATS_EFFECTIVE_ISR_STACK_KB)
ats_clamp_integer("${ATS_DEMO_MAIN_STACK_KB_VALUE}" 8 256 ATS_DEMO_EFFECTIVE_MAIN_STACK_KB)

math(EXPR ATS_BOOT_BRAM_BYTES "${ATS_BOOT_BRAM_KB} * 1024")
math(EXPR ATS_BOARD_RAM_BYTES "${ATS_EFFECTIVE_BOARD_RAM_KB} * 1024")
math(EXPR ATS_DEMO_HEAP_BYTES "${ATS_DEMO_EFFECTIVE_HEAP_KB} * 1024")
math(EXPR ATS_ISR_STACK_BYTES "${ATS_EFFECTIVE_ISR_STACK_KB} * 1024")
math(EXPR ATS_DEMO_MAIN_STACK_BYTES "${ATS_DEMO_EFFECTIVE_MAIN_STACK_KB} * 1024")
math(EXPR ATS_DEMO_MAIN_STACK_WORDS "${ATS_DEMO_MAIN_STACK_BYTES} / 4")

message(STATUS "MPS2 AN385 boot memory=${ATS_BOOT_BRAM_KB}KB, CPU clock=${ATS_CPU_CLOCK_HZ}Hz")
message(STATUS "MPS2 AN385 SRAM budget requested=${ATS_BOARD_RAM_KB_VALUE}KB -> effective RAM=${ATS_EFFECTIVE_BOARD_RAM_KB}KB")
message(STATUS "Requested heap=${ATS_DEMO_HEAP_KB_VALUE}KB -> effective heap=${ATS_DEMO_EFFECTIVE_HEAP_KB}KB")
message(STATUS "Requested ISR stack=${ATS_ISR_STACK_KB_VALUE}KB -> effective ISR stack=${ATS_EFFECTIVE_ISR_STACK_KB}KB")
message(STATUS "Requested main stack=${ATS_DEMO_MAIN_STACK_KB_VALUE}KB -> effective telemetry stack=${ATS_DEMO_EFFECTIVE_MAIN_STACK_KB}KB")

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

list(REMOVE_ITEM SDK_C_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/ats_audio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/ats_fs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/ats_net.c"
)

set(SDK_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/startup_mps2_an385.s"
    ${SDK_C_SOURCE_FILES}
)

add_executable(ats_app
    ${SDK_SOURCE_FILES}
    ${APP_SOURCE_FILES}
)

set_target_properties(ats_app PROPERTIES
    OUTPUT_NAME "ats_app"
    SUFFIX ".elf"
)

target_include_directories(ats_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/source"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/FreeRTOS"
)

target_compile_definitions(ats_app PRIVATE
    ATS=1
    ATS_CPU_CLOCK_HZ=${ATS_CPU_CLOCK_HZ}
    ATS_BOOT_BRAM_BYTES=${ATS_BOOT_BRAM_BYTES}
    ATS_BOARD_RAM_BYTES=${ATS_BOARD_RAM_BYTES}
    ATS_DEMO_HEAP_BYTES=${ATS_DEMO_HEAP_BYTES}
    ATS_ISR_STACK_BYTES=${ATS_ISR_STACK_BYTES}
    ATS_DEMO_DECLARED_MAIN_STACK_BYTES=${ATS_DEMO_MAIN_STACK_BYTES}
    ATS_DEMO_MAIN_STACK_WORDS=${ATS_DEMO_MAIN_STACK_WORDS}
)

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
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ats_app.map
)

add_custom_command(TARGET ats_app POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:ats_app>" "${CMAKE_CURRENT_BINARY_DIR}/ats_app.bin"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/ats_app.bin"
    COMMENT "Generating ats_app.bin"
)
