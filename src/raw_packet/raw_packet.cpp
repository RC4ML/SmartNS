#include "raw_packet/raw_packet.h"
#include "config.h"

uint32_t calculate_soft_rss(ipv4_tuple tuple, const uint8_t *rss_key) {
    uint32_t i, j, map, ret = 0;
    uint32_t input_len = sizeof(ipv4_tuple) / sizeof(uint32_t);
    uint32_t *input_tuple = (uint32_t *)&tuple;
    for (j = 0; j < input_len; j++) {
        for (map = input_tuple[j]; map; map &= (map - 1)) {
            i = (uint32_t)__builtin_ctz(map);
            ret ^= htonl(((const uint32_t *)rss_key)[j]) << (31 - i) |
                (uint32_t)((uint64_t)(htonl(((const uint32_t *)rss_key)[j + 1])) >>
                    (i + 1));
        }
    }
    return ret;
}

uint32_t ip_to_uint32(const char *ip) {
    struct in_addr addr;
    assert(inet_pton(AF_INET, ip, &addr) == 1);
    return ntohl(addr.s_addr);
}

void init_udp_packet(udp_packet *packet, ipv4_tuple tuple, bool is_server) {
    packet->eth_hdr.ether_type = htons(0x0800);
    for (size_t i = 0;i < 6;i++) {
        packet->eth_hdr.src_addr.addr_bytes[i] = is_server ? server_mac[i] : client_mac[i];
        packet->eth_hdr.dst_addr.addr_bytes[i] = is_server ? client_mac[i] : server_mac[i];
    }
    packet->ip_hdr.version_ihl = 0x45;
    packet->ip_hdr.type_of_service = 0;
    packet->ip_hdr.total_length = htons(SMARTNS_TX_PACKET_BUFFER - sizeof(ether_hdr));

    packet->ip_hdr.packet_id = htons(0);
    packet->ip_hdr.fragment_offset = htons(0);
    packet->ip_hdr.time_to_live = 64;
    packet->ip_hdr.next_proto_id = 17;
    packet->ip_hdr.src_addr = htonl(tuple.src_addr);
    packet->ip_hdr.dst_addr = htonl(tuple.dst_addr);

    packet->udp_hdr.src_port = htons(tuple.sport);
    packet->udp_hdr.dst_port = htons(tuple.dport);
    packet->udp_hdr.dgram_len = htons(SMARTNS_TX_PACKET_BUFFER - sizeof(ether_hdr) - sizeof(ipv4_hdr));
}