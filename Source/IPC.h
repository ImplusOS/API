#pragma once

#include <stdint.h>

#define IPC_MESSAGE_MAX_SIZE 256

typedef struct {
    int32_t sender_pid;
    uint32_t size;
    char data[IPC_MESSAGE_MAX_SIZE];
} __attribute__((aligned(16))) ipc_message_t;

int32_t ipc_send_message(int32_t target_pid, const void *message, uint32_t size);
int32_t ipc_receive_message(ipc_message_t *out_message);