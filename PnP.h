#pragma once

#include <stdint.h>

#include "../../Kernel/include/kernel/pnp.h"

int32_t pnp_subscribe(void);
int32_t pnp_unsubscribe(void);
int32_t pnp_drain(void);
