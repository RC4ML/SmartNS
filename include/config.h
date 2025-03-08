#pragma once

#define SMARTNS_TX_RX_CORE 8
#define SMARTNS_CONTROL_CORE 1
#define SMARTNS_DMA_GROUP_SIZE 1
#define SMARTNS_DMA_BATCH (4)

#define SMARTNS_TX_DEPTH 1024
#define SMARTNS_RX_DEPTH 1024
#define SMARTNS_TX_PACKET_BUFFER (128)
#define SMARTNS_RX_PACKET_BUFFER (8192+128)
#define SMARTNS_MTU (8192)
#define SMARTNS_TCP_PORT (6666)
#define SMARTNS_UDP_MAGIC_PORT (23456)

#define SMARTNS_RX_BATCH 16
#define SMARTNS_RX_SEG   1
#define SMARTNS_TX_BATCH 16
#define SMARTNS_TX_SEG   2

#if defined(__x86_64__)
#define SMARTNS_DMA_GID_INDEX 3
#elif defined(__aarch64__)
#define SMARTNS_DMA_GID_INDEX 1
#endif

#define RSS_HASH_KEY_LENGTH 40

// pcie5.0-up client and down server
extern unsigned char client_mac[6];
extern unsigned char server_mac[6];

extern const char *client_ip;
extern const char *server_ip;

extern unsigned char RSS_KEY[RSS_HASH_KEY_LENGTH];