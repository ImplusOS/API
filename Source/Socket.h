#pragma once

#include <stdint.h>

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

typedef struct {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t state;
    int32_t error;
} socket_info_t;

int32_t socket_create(int32_t type);
int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
int32_t socket_bind(int32_t sockfd, uint16_t port);
int32_t socket_listen(int32_t sockfd);
int32_t socket_listen_with_backlog(int32_t sockfd, int32_t backlog);
int32_t socket_accept(int32_t sockfd);
int32_t socket_send(int32_t sockfd, const void *data, uint32_t len);
int32_t socket_recv(int32_t sockfd, void *buf, uint32_t buf_len);
int32_t socket_close(int32_t sockfd);
int32_t socket_get_info(int32_t sockfd, socket_info_t *info_out);
int32_t socket_set_option(int32_t sockfd, int32_t level,
                          int32_t option, int32_t value);
int32_t socket_get_option(int32_t sockfd, int32_t level,
                          int32_t option, int32_t *value_out);
int32_t socket_shutdown(int32_t sockfd, int32_t how);
