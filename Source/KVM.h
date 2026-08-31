#pragma once
#include <stdint.h>
#include <stddef.h>

int32_t kvm_open(void);
int64_t kvm_ioctl(int32_t fd, uint64_t request, uint64_t arg);
int32_t kvm_close(int32_t fd);
void *kvm_mmap(int32_t fd, uint64_t offset, uint64_t size);
