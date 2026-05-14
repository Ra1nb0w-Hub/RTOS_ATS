# ─── mqtt_embed 静态库 ──────────────────────────────────────────────────────────
TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
TARGET = ats_mqtt

SOURCES += \
    mqtt_embed/MQTTPacket/src/MQTTPacket.c \
    mqtt_embed/MQTTPacket/src/MQTTConnectClient.c \
    mqtt_embed/MQTTPacket/src/MQTTDeserializePublish.c \
    mqtt_embed/MQTTPacket/src/MQTTSerializePublish.c \
    mqtt_embed/MQTTPacket/src/MQTTSubscribeClient.c \
    mqtt_embed/MQTTPacket/src/MQTTUnsubscribeClient.c \
    mqtt_embed/MQTTPacket/src/MQTTFormat.c

INCLUDEPATH += mqtt_embed/MQTTPacket/src
