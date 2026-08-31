#pragma once

#include <stdint.h>

extern int os_errno;

int os_get_errno(void);
void os_set_errno(int value);
void os_clear_errno(void);
int os_status_is_error(int64_t status_code);
int os_status_to_errno(int64_t status_code);
