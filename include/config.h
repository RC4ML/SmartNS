#pragma once
#include "common.hpp"

#define SMARTNS_TX_RX_CORE 12
#define SMARTNS_CONTROL_CORE 2
#define SMARTNS_DMA_GROUP_SIZE 6

#define SMARTNS_TX_DEPTH 512
#define SMARTNS_RX_DEPTH 512
#define SMARTNS_TX_PACKET_BUFFER (128)
#define SMARTNS_RX_PACKET_BUFFER (2048)
#define SMARTNS_TCP_PORT 6666

#define SMARTNS_RX_BATCH 16
#define SMARTNS_RX_SEG   1
#define SMARTNS_TX_BATCH 16
#define SMARTNS_TX_SEG   2

#define SMARTNS_DMA_GID_INDEX 1
// pcie5.0-up client and down server
static unsigned char client_mac[6] = { 0xa0, 0x88, 0xc2, 0x31, 0xf7, 0xde };
static unsigned char server_mac[6] = { 0xa0, 0x88, 0xc2, 0x32, 0x04, 0x30 };
