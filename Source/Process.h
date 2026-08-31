#pragma once

#include <stdint.h>

typedef void (*signal_handler_t)(int32_t signum);

signal_handler_t os_signal(int32_t signum, signal_handler_t handler);
void process_yield(void);
int32_t process_get_current_pid(void);
int32_t process_spawn(const char *path);
int32_t process_spawn_with_arg(const char *path, const char *argument);
int32_t process_get_launch_argument(char *buffer, uint32_t capacity);
int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options);
int32_t process_getppid(void);
void process_exit(int32_t status);
void system_shutdown(void);
void system_shutdown_broadcast(void);
void system_reboot(void);
void sleep_ms(uint64_t milliseconds);
uint64_t get_uptime_ms(void);

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    uint8_t state;
    uint8_t reserved[7];
    char    name[64];
    uint64_t total_ticks;
    uint64_t memory_usage;
} process_info_t;

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    uint8_t state;
    uint8_t reserved[7];
    char name[64];
    uint64_t runtime_ns;
    uint64_t ready_wait_ns;
    uint64_t max_ready_wait_ns;
    uint64_t context_switches;
    uint64_t voluntary_switches;
    uint64_t involuntary_switches;
    uint64_t syscalls;
    uint64_t ipc_send;
    uint64_t ipc_recv;
    uint64_t block_count;
    uint64_t wake_count;
    uint64_t blocked_ns;
    uint64_t display_present_calls;
    uint64_t display_rects;
    uint64_t display_bytes;
    uint64_t memory_usage;
    uint64_t page_fault_count;
} process_perf_info_t;

#define BOOT_PROFILE_NAME_MAX 32u
typedef struct {
    char name[BOOT_PROFILE_NAME_MAX];
    uint64_t start_ns;
    uint64_t duration_ns;
} boot_profile_entry_t;

int32_t get_process_count(void);
int32_t get_process_info(int32_t pid, process_info_t *info_out);
int32_t get_process_perf_info(int32_t pid, process_perf_info_t *info_out);
int32_t get_boot_profile_count(void);
int32_t get_boot_profile_entry(int32_t index, boot_profile_entry_t *entry_out);
int32_t process_kill(int32_t pid);
int32_t process_set_priority(int32_t pid, uint32_t priority);
uint64_t get_total_memory(void);
uint64_t get_used_memory(void);
