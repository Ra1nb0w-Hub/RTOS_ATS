# ─── MQTT 库 ──────────────────────────────────────────────────────────
# 构建为静态库, 供 ats_app 链接使用。

file(GLOB MQTT_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/mqtt_embed/MQTTPacket/src/*.c"
)

add_library(mqtt STATIC ${MQTT_SOURCE_FILES})

target_include_directories(mqtt
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/mqtt_embed/MQTTPacket/src"
)

target_compile_options(mqtt PRIVATE
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
