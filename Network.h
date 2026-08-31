#pragma once

#include <stdint.h>
#include <stdbool.h>

bool udp_send(uint32_t dst_ipv4_addr,
              uint16_t src_port,
              uint16_t dst_port,
              const void *payload,
              uint16_t payload_len);

int32_t udp_bind_port(uint16_t port);
int32_t udp_unbind_port(uint16_t port);
int32_t udp_recv(uint16_t port, void *buf, uint32_t buf_len);

uint32_t dns_resolve(const char *hostname);
uint32_t dns_resolve_with_server(const char *hostname, uint32_t dns_server_ip);

#define TCP_STATE_LISTEN        1
#define TCP_STATE_SYN_SENT      2
#define TCP_STATE_SYN_RECEIVED  3
#define TCP_STATE_ESTABLISHED   4
#define TCP_STATE_FIN_WAIT_1    5
#define TCP_STATE_FIN_WAIT_2    6
#define TCP_STATE_CLOSE_WAIT    7
#define TCP_STATE_CLOSING       8
#define TCP_STATE_LAST_ACK      9
#define TCP_STATE_TIME_WAIT     10

int32_t tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
int32_t tcp_listen(uint16_t port);
int32_t tcp_accept(int32_t listen_conn_id);
int32_t tcp_send(int32_t conn_id, const void *data, uint16_t len);
int32_t tcp_recv(int32_t conn_id, void *buf, uint16_t buf_len);
int32_t tcp_close(int32_t conn_id);
int32_t tcp_get_state(int32_t conn_id);