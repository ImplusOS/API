#pragma once

#include <stdint.h>

typedef void (*signal_handler_t)(int32_t signum);

signal_handler_t os_signal(int32_t signum, signal_handler_t handler);
void process_yield(void);
int32_t process_get_current_pid(void);
int32_t process_spawn(const char *path);
int32_t process_spawn_with_arg(const char *path, const char *argument);
/* Spawn with a caller-supplied environment: `envp` is a NULL-terminated array
 * of "NAME=VALUE" pointers (build one with API/Source/LinuxEnv.c), or NULL
 * for the kernel's generic defaults alone. Application-specific environment
 * belongs to the launcher, never to the kernel. */
int32_t process_spawn_with_env(const char *path, const char *argument,
                               const char *const *envp);
/* The uid/gid Linux programs this process spawns from now on will run as.
 * (uint32_t)-1 leaves a value unchanged. */
int32_t process_set_credentials(uint32_t uid, uint32_t gid);
int32_t process_get_launch_argument(char *buffer, uint32_t capacity);
int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options);
int32_t process_getppid(void);
void process_exit(int32_t status);
void system_shutdown(void);
void system_shutdown_broadcast(void);
void system_reboot(void);
void sleep_ms(uint64_t milliseconds);
uint64_t get_uptime_ms(void);

/* reserved[0] carries the scheduling priority (PROCESS_PRIORITY_*). */
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
    /* Thread group: the process this task is a thread of, or its own pid.
     * Add a thread's numbers to that process to get per-process figures. */
    int32_t  owner_pid;
    uint8_t  is_thread;
    uint8_t  priority;           /* PROCESS_PRIORITY_*                     */
    uint8_t  abi_mode;           /* PROCESS_ABI_* below                    */
    uint8_t  reserved2;
    uint64_t disk_read_bytes;    /* bytes its requests read from disks     */
    uint64_t disk_write_bytes;
    uint64_t gpu_3d_ns;          /* GPU 3D-engine time its work occupied   */
    uint64_t gpu_copy_ns;        /* time its presents kept scanout busy    */
} process_perf_info_t;

/* process_perf_info_t.abi_mode (the kernel's PROCESS_ABI_*) */
#ifndef PROCESS_ABI_IMPLUS
#define PROCESS_ABI_IMPLUS      0u
#define PROCESS_ABI_LINUX       1u   /* x86_64 Linux                       */
#define PROCESS_ABI_LINUX_I386  2u
#define PROCESS_ABI_LINUX_ARM64 3u
#endif

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
/* Send `signum` (Linux numbering: 15 = SIGTERM) to `pid`. process_kill() is
 * this with SIGKILL. A negative `pid` names a session (or process group):
 * -N reaches every process whose session or group is N, i.e. everything a
 * spawned program N started without leaving its session. signum 0 only
 * asks whether any such process is left (0: yes). */
int32_t process_send_signal(int32_t pid, int32_t signum);
/* Priority is a timeslice multiplier: 0 = 1x, 1 = 1.5x (the default every
 * process starts with), 2 = 2x, 3 = 3x. */
#define PROCESS_PRIORITY_LOW     0u
#define PROCESS_PRIORITY_NORMAL  1u
#define PROCESS_PRIORITY_HIGH    2u
#define PROCESS_PRIORITY_HIGHEST 3u
int32_t process_set_priority(int32_t pid, uint32_t priority);
uint64_t get_total_memory(void);
uint64_t get_used_memory(void);
