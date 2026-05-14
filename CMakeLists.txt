cmake_minimum_required(VERSION 3.16)
project(ATS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets Network Xml REQUIRED)

# ─── ATS framework sources ────────────────────────────────────────────────────
set(ATS_SOURCES
    qt_main.cpp
    ui/MainWindow.cpp
    ui/MainWindow.ui

    core/TestCase.cpp
    core/TestRunner.cpp
    core/AppThread.cpp
    core/KeySimulator.cpp
    log/LogManager.cpp
    TestCase/TestNetStatus.cpp
)

set(ATS_HEADERS
    ui/MainWindow.h

    core/TestCase.h
    core/TestRunner.h
    core/AppThread.h
    core/KeySimulator.h
    log/LogManager.h
)

# ─── ATS API layer (platform adaptation: net / sys / audio / fs / printer) ────────
set(API_SOURCES
    api/ats_net.c
    api/ats_audio.c
    api/ats_fs.c
    api/ats_sys.c
    api/ats_printer.c
    api/ats_lcd.c
)

set(API_HEADERS
    api/ats_net.h
    api/ats_audio.h
    api/ats_fs.h
    api/ats_sys.h
    api/ats_printer.h
    api/ats_lcd.h
)

# ─── mbedtls 静态库 ────────────────────────────────────────────────────────
set(MBEDTLS_SOURCES
    mbedtls/library/aes.c
    mbedtls/library/aesni.c
    mbedtls/library/asn1parse.c
    mbedtls/library/asn1write.c
    mbedtls/library/base64.c
    mbedtls/library/bignum.c
    mbedtls/library/bignum_core.c
    mbedtls/library/bignum_mod.c
    mbedtls/library/bignum_mod_raw.c
    mbedtls/library/block_cipher.c
    mbedtls/library/cipher.c
    mbedtls/library/cipher_wrap.c
    mbedtls/library/constant_time.c
    mbedtls/library/ctr_drbg.c
    mbedtls/library/debug.c
    mbedtls/library/des.c
    mbedtls/library/entropy.c
    mbedtls/library/entropy_poll.c
    mbedtls/library/error.c
    mbedtls/library/md.c
    mbedtls/library/md5.c
    mbedtls/library/net_sockets.c
    mbedtls/library/oid.c
    mbedtls/library/pem.c
    mbedtls/library/pk.c
    mbedtls/library/pk_ecc.c
    mbedtls/library/pk_wrap.c
    mbedtls/library/pkparse.c
    mbedtls/library/pkwrite.c
    mbedtls/library/platform.c
    mbedtls/library/platform_util.c
    mbedtls/library/rsa.c
    mbedtls/library/rsa_alt_helpers.c
    mbedtls/library/sha1.c
    mbedtls/library/sha256.c
    mbedtls/library/sha512.c
    mbedtls/library/ssl_cache.c
    mbedtls/library/ssl_ciphersuites.c
    mbedtls/library/ssl_client.c
    mbedtls/library/ssl_debug_helpers_generated.c
    mbedtls/library/ssl_msg.c
    mbedtls/library/ssl_tls.c
    mbedtls/library/ssl_tls12_client.c
    mbedtls/library/ssl_tls12_server.c
    mbedtls/library/ssl_tls13_client.c
    mbedtls/library/ssl_tls13_generic.c
    mbedtls/library/ssl_tls13_keys.c
    mbedtls/library/ssl_tls13_server.c
    mbedtls/library/threading.c
    mbedtls/library/timing.c
    mbedtls/library/version.c
    mbedtls/library/version_features.c
    mbedtls/library/x509.c
    mbedtls/library/x509_crt.c
    mbedtls/library/x509_crl.c
    mbedtls/library/x509_csr.c
    mbedtls/library/ecp.c
    mbedtls/library/ecp_curves.c
    mbedtls/library/ecp_curves_new.c
    mbedtls/library/ecdh.c
    mbedtls/library/ecdsa.c
    mbedtls/library/mps_reader.c
    mbedtls/library/mps_trace.c
    mbedtls/library/psa_util.c
)

add_library(ats_mbedtls STATIC
    ${MBEDTLS_SOURCES}
)
target_include_directories(ats_mbedtls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
)

# ─── mqtt_embed 静态库 ────────────────────────────────────────────────────
set(MQTT_SOURCES
    mqtt_embed/MQTTPacket/src/MQTTPacket.c
    mqtt_embed/MQTTPacket/src/MQTTConnectClient.c
    mqtt_embed/MQTTPacket/src/MQTTDeserializePublish.c
    mqtt_embed/MQTTPacket/src/MQTTSerializePublish.c
    mqtt_embed/MQTTPacket/src/MQTTSubscribeClient.c
    mqtt_embed/MQTTPacket/src/MQTTUnsubscribeClient.c
    mqtt_embed/MQTTPacket/src/MQTTFormat.c
)

add_library(ats_mqtt STATIC
    ${MQTT_SOURCES}
)
target_include_directories(ats_mqtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt_embed/MQTTPacket/src
)

# ─── App source files (embedded C code) ───────────────────────────────────────
set(APP_SOURCES
    ../apps/OPay/cJSON.c
    ../apps/OPay/OPayAudio.c
    ../apps/OPay/OPayConfig.c
    ../apps/OPay/OPayDevice.c
    ../apps/OPay/OPayFile.c
    ../apps/OPay/OPayKeypad.c
    ../apps/OPay/OPayMenu.c
    ../apps/OPay/OPayMQTT.c
    ../apps/OPay/OPayNetwork.c
    ../apps/OPay/OPayPublic.c
    ../apps/OPay/OPayThread.c
    ../apps/OPay/OPayTracer.c
    ../apps/OPay/OPayTransaction.c
    ../apps/OPay/OPayPrinter.c
    ../apps/OPay/main.c
)

add_executable(ATS
    ${ATS_SOURCES}
    ${ATS_HEADERS}
    ${API_SOURCES}
    ${API_HEADERS}
    ${APP_SOURCES}
)

target_link_libraries(ATS PRIVATE
    ats_mbedtls
    ats_mqtt
)

target_include_directories(ATS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../apps/OPay
    ${CMAKE_CURRENT_SOURCE_DIR}/api
)

target_compile_definitions(ATS PRIVATE
    ATS_SIMULATOR
    ATS
)

target_link_libraries(ATS PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Xml
)

# ─── Windows 特定链接库 ────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(ATS PRIVATE ws2_32 winmm ole32)
endif()
