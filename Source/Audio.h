#pragma once

#include <stdint.h>

#define OS_AUDIO_FORMAT_S16_LE 1u

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t format;
    uint32_t period_bytes;
    uint32_t period_count;
} os_audio_info_t;

int32_t os_audio_open(void);
int32_t os_audio_get_info(os_audio_info_t *out_info);
int64_t os_audio_write(const void *pcm, uint64_t bytes);
int32_t os_audio_drain(uint32_t timeout_ms);
int32_t os_audio_close(void);
