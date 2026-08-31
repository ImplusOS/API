#pragma once

/*
 * WiFi.h -- userland surface for the Wi-Fi management plane exposed by
 * whichever active NIC driver supports it (today: AX900, see
 * Kernel/Drivers/Wi-Fi/AX900/AX900.c). Syscall numbers and
 * semantics are defined in Kernel/Core/syscall/Syscall_Main.h
 * (SYSCALL_WIFI_*) and Syscall_Dispatch.c.
 *
 * The structs below are a byte-for-byte mirror of driver_wifi_scan_result_t
 * / driver_wifi_status_t (Kernel/Drivers/Module/DriverBinary.h) -- kept as
 * a separate, packed definition here rather than a shared header because
 * Userland is a distinct freestanding build from the kernel (see
 * Userland/API/SystemInfo.h for the same convention with system_info_t
 * and friends). Keep the two in sync by hand if either changes.
 */

#include <stdbool.h>
#include <stdint.h>

#define WIFI_SSID_MAX 32u

typedef enum {
    WIFI_SECURITY_UNKNOWN = 0,
    WIFI_SECURITY_OPEN,
    WIFI_SECURITY_WPA_PSK,
} wifi_security_t;

typedef struct __attribute__((packed)) {
    char ssid[WIFI_SSID_MAX + 1u];
    uint8_t bssid[6];
    int8_t  rssi_dbm;
    uint32_t security; /* wifi_security_t */
} wifi_scan_result_t;

typedef enum {
    WIFI_STATE_NO_ADAPTER = 0,
    WIFI_STATE_ADAPTER_ATTACHED,
    WIFI_STATE_FIRMWARE_LOADING,
    WIFI_STATE_FIRMWARE_FAILED,
    WIFI_STATE_READY,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_ASSOCIATED,
    WIFI_STATE_CONNECT_FAILED,
} wifi_state_t;

typedef struct __attribute__((packed)) {
    uint32_t state; /* wifi_state_t */
    char ssid[WIFI_SSID_MAX + 1u];
    uint8_t mac[6];
} wifi_status_t;

#define WIFI_MAX_SCAN_RESULTS 24u

/* Kicks off an async scan; results accumulate over the next ~3s and are
 * read back with wifi_get_scan_results(). Returns false if there's no
 * Wi-Fi-capable NIC active, or its firmware isn't up yet. */
bool wifi_scan_start(void);

/* Returns the number of entries written to `out` (<= max_count, and
 * <= WIFI_MAX_SCAN_RESULTS regardless of what max_count requests). */
uint32_t wifi_get_scan_results(wifi_scan_result_t *out, uint32_t max_count);

/* psk == NULL connects to an open network. A non-NULL psk (8-63 ASCII
 * chars, per IEEE 802.11-2020) attempts WPA2-PSK -- see AX900.c /
 * AX900_Protocol.h for why that path is labeled experimental: the RSN IE
 * this driver builds is spec-correct, but exactly how the passphrase
 * reaches AICSemi's firmware over this transport is unconfirmed. Returns
 * once the driver has *sent* the connect request and gotten a first
 * positive ack, not once actually associated -- poll wifi_get_status()
 * for that. */
bool wifi_connect(const char *ssid, const char *psk);
void wifi_disconnect(void);
void wifi_get_status(wifi_status_t *out_status);

/* DHCP-learned DNS server (0 if no lease yet / not using DHCP) -- see
 * Kernel/Network/network_main.c: dns_resolve() falls back to this ahead
 * of NetworkStack/DNS's hardcoded QEMU-NAT default once a lease exists. */
uint32_t net_get_dhcp_dns_server(void);
