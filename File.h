#pragma once

#include <stdint.h>

typedef struct {
    char     name[260];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t  attributes;
} file_dirent_t;

int32_t file_open(const char *path, uint64_t flags);
int32_t file_creat(const char *path);
int64_t file_read(int32_t fd, void *buffer, uint64_t len);
int64_t file_write(int32_t fd, const void *buffer, uint64_t len);
int64_t file_seek(int32_t fd, int64_t offset, int32_t whence);
int32_t file_close(int32_t fd);
int32_t file_mkdir(const char *path);
int32_t file_opendir(const char *path);
int32_t file_readdir(int32_t dir_handle, file_dirent_t *out_entry);
int32_t file_closedir(int32_t dir_handle);
int32_t file_unlink(const char *path);
int32_t file_rename(const char *old_path, const char *new_path);

typedef struct {
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  exists;
} file_stat_t;

int32_t file_stat(const char *path, file_stat_t *stat_out);
int32_t file_pipe(int32_t fds[2]);
int32_t file_dup(int32_t oldfd);
int32_t file_dup2(int32_t oldfd, int32_t newfd);
