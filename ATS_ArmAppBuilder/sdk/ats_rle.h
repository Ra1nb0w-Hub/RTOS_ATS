#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

uint16_t ats_rle8_worst_case_size(uint16_t source_length);
uint16_t ats_rle8_encode(const uint8_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity);

uint16_t ats_rle16_worst_case_size(uint16_t source_length);
uint16_t ats_rle16_encode(const uint16_t *source, uint16_t source_length, uint8_t *destination, uint16_t destination_capacity);

#ifdef __cplusplus
}
#endif