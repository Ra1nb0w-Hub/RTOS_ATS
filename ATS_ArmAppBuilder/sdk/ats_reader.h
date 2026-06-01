#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "emv_lib/include/emv_api.h"

int ats_reader_init(void);
int ats_reader_open(void);
int ats_reader_close(void);
int ats_reader_poll(EMVInterfaceType *card_interface, unsigned int timeout_ms);
int ats_reader_cancel(void);

int ats_reader_icc_power_on(unsigned char *atr, size_t *atr_len);
int ats_reader_icc_power_off(void);
int ats_reader_icc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

int ats_reader_picc_activate(unsigned char *ats, size_t *ats_len);
int ats_reader_picc_deactivate(void);
int ats_reader_picc_transceive_apdu(const unsigned char *command, size_t command_len, unsigned char *response, size_t *response_len);

int ats_reader_get_last_hw_error(void);

#ifdef __cplusplus
}
#endif
