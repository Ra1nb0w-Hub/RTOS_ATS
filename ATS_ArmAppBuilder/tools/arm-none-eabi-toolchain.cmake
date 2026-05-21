set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

get_filename_component(ATS_ARMAPPBUILDER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(DEFINED ENV{ATS_ARM_TOOLCHAIN_BIN_DIR} AND NOT "$ENV{ATS_ARM_TOOLCHAIN_BIN_DIR}" STREQUAL "")
    set(ATS_LOCAL_TOOLCHAIN_BIN_DIR "$ENV{ATS_ARM_TOOLCHAIN_BIN_DIR}")
else()
    set(ATS_LOCAL_TOOLCHAIN_BIN_DIR "${ATS_ARMAPPBUILDER_DIR}/tools/gcc-arm-none-eabi-lite/bin")
endif()

function(ats_resolve_program OUTPUT_NAME ENV_NAME LOCAL_FILE_NAME PROGRAM_NAME)
    set(_resolved "")

    if(DEFINED ENV{${ENV_NAME}} AND NOT "$ENV{${ENV_NAME}}" STREQUAL "")
        set(_resolved "$ENV{${ENV_NAME}}")
    elseif(EXISTS "${ATS_LOCAL_TOOLCHAIN_BIN_DIR}/${LOCAL_FILE_NAME}")
        set(_resolved "${ATS_LOCAL_TOOLCHAIN_BIN_DIR}/${LOCAL_FILE_NAME}")
    else()
        find_program(_resolved "${PROGRAM_NAME}")
    endif()

    if(NOT _resolved)
        message(FATAL_ERROR "${PROGRAM_NAME} not found. Add it to PATH or set ${ENV_NAME}.")
    endif()

    set(${OUTPUT_NAME} "${_resolved}" PARENT_SCOPE)
endfunction()

ats_resolve_program(ATS_ARM_GCC ATS_ARM_GCC arm-none-eabi-gcc.exe arm-none-eabi-gcc)
ats_resolve_program(ATS_ARM_AR ATS_ARM_AR arm-none-eabi-ar.exe arm-none-eabi-ar)
ats_resolve_program(ATS_ARM_RANLIB ATS_ARM_RANLIB arm-none-eabi-ranlib.exe arm-none-eabi-ranlib)
ats_resolve_program(ATS_ARM_OBJCOPY ATS_ARM_OBJCOPY arm-none-eabi-objcopy.exe arm-none-eabi-objcopy)

get_filename_component(ATS_ARM_TOOLCHAIN_BIN_DIR "${ATS_ARM_GCC}" DIRECTORY)
set(ENV{PATH} "${ATS_ARM_TOOLCHAIN_BIN_DIR};$ENV{PATH}")

set(CMAKE_C_COMPILER "${ATS_ARM_GCC}" CACHE FILEPATH "ARM GCC C compiler")
set(CMAKE_ASM_COMPILER "${ATS_ARM_GCC}" CACHE FILEPATH "ARM GCC ASM compiler")
set(CMAKE_AR "${ATS_ARM_AR}" CACHE FILEPATH "ARM GCC archiver")
set(CMAKE_RANLIB "${ATS_ARM_RANLIB}" CACHE FILEPATH "ARM GCC ranlib")
set(CMAKE_OBJCOPY "${ATS_ARM_OBJCOPY}" CACHE FILEPATH "ARM GCC objcopy")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
