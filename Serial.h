#pragma once

#include <stdint.h>

void serial_write_char(char c);
void serial_write_string(const char *str);
void serial_write_uint64(uint64_t value);
void serial_write_uint32(uint32_t value);
void serial_write_uint16(uint16_t value);
