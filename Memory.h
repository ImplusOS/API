#pragma once

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void  free(void *ptr);
void *os_mmap(uint64_t length, uint64_t flags);
int32_t os_shared_memory_create(uint32_t size);
int32_t os_shared_memory_grant(int32_t handle, int32_t pid);
void *os_shared_memory_map(int32_t handle);
int32_t os_shared_memory_unmap(int32_t handle, void *address);
int32_t os_shared_memory_close(int32_t handle);
/* Shared-memory handle backing a memfd fd (e.g. one received over an
 * AF_UNIX socket via SCM_RIGHTS), or <0. Map it with
 * os_shared_memory_map(). */
int32_t os_memfd_shm_handle(int32_t fd);
void *memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *ptr, int value, size_t num);

size_t os_strnlen(const char *str, size_t max_len);
int os_strcpy_s(char *dst, size_t dst_size, const char *src);
int os_strcat_s(char *dst, size_t dst_size, const char *src);
