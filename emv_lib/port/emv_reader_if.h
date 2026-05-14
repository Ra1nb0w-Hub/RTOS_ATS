#ifndef EMV_READER_IF_H
#define EMV_READER_IF_H

#include "emv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EMVReaderInterface {
    void *user_data;

    int (*open)(void *user_data);
    int (*close)(void *user_data);
    bool (*get_status)(void *user_data);
    int (*poll_card)(void *user_data, EMVInterfaceType *card_interface, unsigned int timeout_ms);
    int (*cancel_io)(void *user_data);

    int (*icc_power_on)(void *user_data, unsigned char *atr, size_t *atr_len);
    int (*icc_power_off)(void *user_data);
    int (*icc_transceive_apdu)(void *user_data,
                               const unsigned char *command,
                               size_t command_len,
                               unsigned char *response,
                               size_t *response_len);

    int (*picc_activate)(void *user_data, unsigned char *ats, size_t *ats_len);
    int (*picc_deactivate)(void *user_data);
    int (*picc_transceive_apdu)(void *user_data,
                                const unsigned char *command,
                                size_t command_len,
                                unsigned char *response,
                                size_t *response_len);

    int (*get_last_hw_error)(void *user_data);
} EMVReaderInterface;

#ifdef __cplusplus
}
#endif

#endif
