#pragma once
#include "common.hpp"

#define SMARTNS_TX_RX_CORE 12
#define SMARTNS_CONTROL_CORE 2
#define SMARTNS_DMA_GROUP_SIZE 6

#define SMARTNS_TX_DEPTH 512
#define SMARTNS_RX_DEPTH 512
#define SMARTNS_TX_PACKET_BUFFER (128)
#define SMARTNS_RX_PACKET_BUFFER (2048)
#define SMARTNS_TCP_PORT (6666)
#define SMARTNS_UDP_MAGIC_PORT (23456)

#define SMARTNS_RX_BATCH 16
#define SMARTNS_RX_SEG   1
#define SMARTNS_TX_BATCH 16
#define SMARTNS_TX_SEG   2

#define SMARTNS_DMA_GID_INDEX 1

#define RSS_HASH_KEY_LENGTH 40

// pcie5.0-up client and down server
extern unsigned char client_mac[6];
extern unsigned char server_mac[6];

extern uint8_t RSS_KEY[RSS_HASH_KEY_LENGTH];