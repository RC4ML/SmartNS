#pragma once

#include "common.hpp"

struct ether_addr {
    uint8_t addr_bytes[6];
};

struct ether_hdr {
    struct ether_addr dst_addr;
    struct ether_addr src_addr;
    uint16_t ether_type;
} __attribute__((__packed__));

struct ipv4_hdr {
    uint8_t version_ihl;
    uint8_t type_of_service;
    uint16_t total_length;
    uint16_t packet_id;
    uint16_t fragment_offset;
    uint8_t time_to_live;
    uint8_t next_proto_id;
    uint16_t hdr_checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} __attribute__((__packed__));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t dgram_len;
    uint16_t dgram_cksum;
} __attribute__((__packed__));


struct udp_packet {
    struct ether_hdr eth_hdr;
    struct ipv4_hdr ip_hdr;
    struct udp_hdr udp_hdr;
    // no data element entry
} __attribute__((__packed__));


// must be CPU Byte order 
struct ipv4_tuple {
    uint32_t	src_addr;
    uint32_t	dst_addr;
    union {
        struct {
            uint16_t dport;
            uint16_t sport;
        };
        uint32_t        sctp_tag;
    };
};

uint32_t calculate_soft_rss(ipv4_tuple tuple, const uint8_t *rss_key);

uint32_t ip_to_uint32(const char *ip);

void init_udp_packet(udp_packet *packet, ipv4_tuple tuple, bool is_server);