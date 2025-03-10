// Client: sudo ./arm_relay_1_1 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 0 -threads 1 -payload_size 1024 -serverIp 10.0.0.101
// Relay:  sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 1 -payload_size 1024
// Server: sudo ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 2 -threads 1 -payload_size 1024

#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"
#include "raw_packet/raw_packet.h"

std::atomic<bool> stop_flag = false;
std::mutex IO_LOCK;
static uint64_t NB_RXD = 1024;
static uint64_t NB_TXD = 1024;
static uint64_t PKT_BUF_SIZE = 8448;
static uint64_t PKT_HANDLE_BATCH = 4;
static uint64_t PKT_SEND_OUTSTANDING = 64;

static uint64_t BUF_SIZE = (NB_TXD + NB_RXD) * PKT_BUF_SIZE;

static uint64_t FLOW_UDP_DST_PORT = 6666;

DEFINE_int32(nodeType, 100, "0: client, 1: relay, 2: server");
enum NodeType {
    CLIENT = 0,
    RELAY = 1,
    SERVER = 2,
};

// TODO check
// bf1 enp3s0f0s0
unsigned char SERVER_MAC_ADDR[6] = { 0x02,0x15,0x9e,0x7c,0x4d,0xad };
// bf2 enp3s0f0s0
unsigned char CLIENT_MAC_ADDR[6] = { 0x02,0xbd,0xe9,0x97,0x48,0xd3 };

void ctrl_c_handler(int) { stop_flag = true; }

void init_raw_packet_handler(qp_handler *handler, size_t thread_index) {
    size_t local_mr_addr = reinterpret_cast<size_t>(handler->buf);
    size_t local_rkey = handler->mr->lkey;

    size_t tx_depth = handler->tx_depth;

    for (int i = 0;i < handler->num_wrs;i++) {
        handler->send_sge_list[i].lkey = local_rkey;
        handler->send_wr[i].sg_list = handler->send_sge_list + i;
        handler->send_wr[i].num_sge = 1;
        if (i != 0) {
            handler->send_wr[i - 1].next = handler->send_wr + i;
        }
        handler->send_wr[i].next = nullptr;
        handler->send_wr[i].send_flags = IBV_SEND_IP_CSUM;
        handler->send_wr[i].opcode = IBV_WR_SEND;
    }

    for (size_t i = 0;i < tx_depth;i++) {
        udp_packet *now_pkt = reinterpret_cast<udp_packet *>(local_mr_addr + PKT_BUF_SIZE * i);
        now_pkt->eth_hdr.ether_type = htons(0x0800);
        for (size_t j = 0;j < 6;j++) {
            now_pkt->eth_hdr.dst_addr.addr_bytes[j] = SERVER_MAC_ADDR[j];
            now_pkt->eth_hdr.src_addr.addr_bytes[j] = CLIENT_MAC_ADDR[j];
        }

        now_pkt->ip_hdr.version_ihl = 0x45;
        now_pkt->ip_hdr.type_of_service = 0;
        now_pkt->ip_hdr.total_length = htons(FLAGS_payload_size - sizeof(ether_hdr));
        now_pkt->ip_hdr.packet_id = htons(0);
        now_pkt->ip_hdr.fragment_offset = htons(0);
        now_pkt->ip_hdr.time_to_live = 64;
        now_pkt->ip_hdr.next_proto_id = 17;
        now_pkt->ip_hdr.src_addr = 0;
        now_pkt->ip_hdr.dst_addr = 0;

        now_pkt->udp_hdr.dgram_len = htons(FLAGS_payload_size - sizeof(ether_hdr) - sizeof(ipv4_hdr));
        now_pkt->udp_hdr.src_port = FLOW_UDP_DST_PORT;
        now_pkt->udp_hdr.dst_port = htons(FLOW_UDP_DST_PORT + thread_index);
    }

}

/**
 * @brief Relay thread
 *
 * - Fetch data from client
 * - Send data to server
 *
 * @param[in] thread_index NUMA local index that the thread will be bind to
 */

void sub_task_relay(size_t thread_index, qp_handler *handler_server, qp_handler *handler_client) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(1);
    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler send_client(NB_TXD, PKT_BUF_SIZE, 64);
    offset_handler send_client_comp(NB_TXD, PKT_BUF_SIZE, 64);
    offset_handler send_server(NB_TXD, PKT_BUF_SIZE, 0);
    offset_handler send_server_comp(NB_TXD, PKT_BUF_SIZE, 0);

    size_t tx_depth = FLAGS_outstanding;
    size_t ops = FLAGS_iterations * (BUF_SIZE) / PKT_BUF_SIZE;
    ops = round_up(ops, FLAGS_batch_size);
    ops = round_up(ops, SEND_CQ_BATCH);

    for (size_t i = 0; i < tx_depth; i++) {
        // RDMA read from client
        post_send_batch(*handler_client, 1, send_client, FLAGS_payload_size - 64);
    }

    size_t ne_send_client, ne_send_server;

    size_t batch_size = FLAGS_batch_size;
    int done = 0;
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    while (!done && !stop_flag) {
        ne_send_client = poll_send_cq(*handler_client, wc_send);
        for (size_t i = 0; i < ne_send_client; i++) {
            // printf("wc status %s\n", ibv_wc_status_str(wc_send_client[i].status));
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            // printf("client send comp index %ld\n", send_client_comp.index());
            send_client_comp.step(1);
        }

        ne_send_server = poll_send_cq(*handler_server, wc_send);
        for (size_t i = 0; i < ne_send_server; i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            // printf("server send comp index %ld\n", send_comp_server.index());
            send_server_comp.step(PKT_HANDLE_BATCH);
        }

        //plus one cc
        if (send_client.index() < ops && send_server.index() - send_server_comp.index() <= PKT_SEND_OUTSTANDING && send_client.index() - send_client_comp.index() <= tx_depth - batch_size) {
            size_t now_send_num = std::min(ops - send_client.index(), batch_size);
            post_send_batch(*handler_client, now_send_num, send_client, FLAGS_payload_size - 64);
        }

        // TODO double check
        if (send_client_comp.index() - send_server.index() >= PKT_HANDLE_BATCH) {
            for (size_t i = 0;i < PKT_HANDLE_BATCH;i++) {
                handler_server->send_sge_list[i].addr = send_server.offset() + reinterpret_cast<size_t>(handler_server->buf);
                handler_server->send_sge_list[i].length = FLAGS_payload_size;
                handler_client->send_wr[i].wr_id = send_server.index();
                if (i == PKT_HANDLE_BATCH - 1) {
                    handler_server->send_wr[i].next = nullptr;
                    handler_server->send_wr[i].send_flags |= IBV_SEND_SIGNALED;
                }
                send_server.step();
            }
            assert(ibv_post_send(handler_server->qp, handler_server->send_wr, &handler_server->send_bar_wr) == 0);
        }

        if (send_client_comp.index() >= ops) {
            done = 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_client.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    printf("thread [%ld], duration [%f]s, throughput [%f] Gbps\n", thread_index, duration, speed);

    free(wc_send);
}

/**
 * @brief Client thread
 *
 * - Do nothing
 */
void sub_task_client(size_t thread_index, qp_handler *qp_handler) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    while (!stop_flag) {
        sleep(1);
    }
}

/**
 * @brief Server thread
 *
 * - Do nothing
 */
void sub_task_server(size_t thread_index, qp_handler *handler) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    struct ibv_wc *wc_recv = NULL;
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    size_t local_mr_addr = reinterpret_cast<size_t>(handler->buf);
    size_t local_rkey = handler->mr->lkey;
    offset_handler recv(NB_RXD, PKT_BUF_SIZE, NB_TXD * PKT_BUF_SIZE);
    offset_handler recv_comp(NB_RXD, PKT_BUF_SIZE, NB_TXD * PKT_BUF_SIZE);

    size_t rx_depth = handler->rx_depth;

    for (size_t i = 0;i < rx_depth; i++) {
        handler->recv_sge_list[0].addr = recv.offset() + local_mr_addr;
        handler->recv_sge_list[0].length = PKT_BUF_SIZE;
        handler->recv_sge_list[0].lkey = local_rkey;
        handler->recv_wr->num_sge = 1;
        handler->recv_wr->sg_list = handler->recv_sge_list;
        handler->recv_wr->wr_id = recv.offset() + local_mr_addr;
        handler->recv_wr->next = nullptr;

        assert(ibv_post_recv(handler->qp, handler->recv_wr, &handler->recv_bar_wr) == 0);
        recv.step();
    }

    for (int i = 0;i < handler->num_wrs;i++) {
        handler->recv_sge_list[i].lkey = local_rkey;
        handler->recv_wr[i].sg_list = handler->recv_sge_list + i;
        handler->recv_wr[i].num_sge = 1;
        if (i != 0) {
            handler->recv_wr[i - 1].next = handler->recv_wr + i;
        }
        handler->recv_wr[i].next = nullptr;
    }

    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    while (!stop_flag) {
        if ((recv.index() - recv_comp.index()) < (rx_depth - PKT_HANDLE_BATCH)) {
            for (size_t i = 0;i < PKT_HANDLE_BATCH;i++) {
                handler->recv_sge_list[i].addr = recv.offset() + local_mr_addr;
                handler->recv_sge_list[i].length = PKT_BUF_SIZE;
                handler->recv_wr[i].wr_id = recv.offset() + local_mr_addr;
                if (i == PKT_HANDLE_BATCH - 1) {
                    handler->recv_wr[i].next = nullptr;
                }
                recv.step();
            }
            assert(ibv_post_recv(handler->qp, handler->recv_wr, &handler->recv_bar_wr) == 0);
        }
        int ne_recv = ibv_poll_cq(handler->recv_cq, CTX_POLL_BATCH, wc_recv);
        if (ne_recv != 0 && begin_time.tv_sec == 0) {
            clock_gettime(CLOCK_MONOTONIC, &begin_time);
        }
        for (int i = 0;i < ne_recv;i++) {
            assert(wc_recv[i].status == IBV_WC_SUCCESS);
            assert(wc_recv[i].byte_len == static_cast<uint32_t>(FLAGS_payload_size));
            recv_comp.step();
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double recv_speed = 8.0 * recv_comp.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;
    std::lock_guard<std::mutex> lock(IO_LOCK);
    printf("thread [%ld], duration [%f]s, recv speed [%f] Gbps", thread_index, duration, recv_speed);

    free(wc_recv);
}

void benchmark() {
    qp_handler **qp_handlers_client = nullptr;
    qp_handler **qp_handlers_server = nullptr;
    ibv_flow **main_flow = nullptr;

    void **bufs = new void *[FLAGS_threads];
    for (size_t i = 0; i < FLAGS_threads; i++) {
        bufs[i] = get_huge_mem(FLAGS_numaNode, BUF_SIZE);
        for (size_t j = 0; j < BUF_SIZE / (sizeof(int)); j++) {
            (reinterpret_cast<int **>(bufs))[i][j] = j;
        }
    }

    rdma_param rdma_param;
    rdma_param.device_name = FLAGS_deviceName;
    rdma_param.numa_node = FLAGS_numaNode;
    rdma_param.batch_size = std::max(FLAGS_batch_size, PKT_HANDLE_BATCH);
    rdma_param.sge_per_wr = 1;
    roce_init(rdma_param, FLAGS_threads);

    // setup RC QP
    if (FLAGS_nodeType == RELAY || FLAGS_nodeType == CLIENT) {
        tcp_param net_param;
        net_param.isServer = FLAGS_nodeType == CLIENT ? false : true;
        net_param.serverIp = FLAGS_serverIp;
        net_param.sock_port = FLAGS_port;
        socket_init(net_param);


        pingpong_info *info = new pingpong_info[2 * FLAGS_threads]();
        qp_handlers_client = new qp_handler * [FLAGS_threads];
        for (size_t i = 0; i < FLAGS_threads; i++) {
            qp_handlers_client[i] = create_qp_rc(rdma_param, bufs[i], BUF_SIZE, info + i, i);
        }
        exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

        for (size_t i = 0; i < FLAGS_threads; i++) {
            connect_qp_rc(rdma_param, *qp_handlers_client[i], info + i + FLAGS_threads, info + i);
        }

        delete[] info;
        socket_close(net_param);
    }

    if (FLAGS_nodeType == RELAY || FLAGS_nodeType == SERVER) {
        qp_handlers_server = new qp_handler * [FLAGS_threads];
        for (size_t i = 0;i < FLAGS_threads;i++) {
            qp_handlers_server[i] = create_qp_raw_packet(rdma_param, bufs[i], BUF_SIZE, NB_TXD, NB_RXD, i);
        }

        main_flow = new ibv_flow * [FLAGS_threads];

        size_t flow_attr_total_size = sizeof(ibv_flow_attr) + sizeof(ibv_flow_spec_eth) + sizeof(ibv_flow_spec_tcp_udp);

        void *header_buff = malloc(flow_attr_total_size);
        memset(header_buff, 0, flow_attr_total_size);
        ibv_flow_attr *flow_attr = reinterpret_cast<ibv_flow_attr *>(header_buff);
        ibv_flow_spec_eth *flow_spec_eth = reinterpret_cast<ibv_flow_spec_eth *>(flow_attr + 1);
        ibv_flow_spec_tcp_udp *flow_spec_udp = reinterpret_cast<ibv_flow_spec_tcp_udp *>(flow_spec_eth + 1);
        flow_attr->size = flow_attr_total_size;
        flow_attr->priority = 0;
        flow_attr->num_of_specs = 2;
        flow_attr->port = 1;
        flow_attr->flags = 0;
        flow_attr->type = IBV_FLOW_ATTR_NORMAL;

        flow_spec_eth->type = IBV_FLOW_SPEC_ETH;
        flow_spec_eth->size = sizeof(ibv_flow_spec_eth);
        flow_spec_eth->val.ether_type = htons(0x0800);
        flow_spec_eth->mask.ether_type = 0xffff;
        if (FLAGS_nodeType == SERVER) {
            memcpy(flow_spec_eth->val.dst_mac, SERVER_MAC_ADDR, 6);
        } else {
            memcpy(flow_spec_eth->val.dst_mac, CLIENT_MAC_ADDR, 6);
        }
        memset(flow_spec_eth->mask.dst_mac, 0xFF, 6);

        for (size_t i = 0;i < FLAGS_threads;i++) {
            flow_spec_udp->type = IBV_FLOW_SPEC_UDP;
            flow_spec_udp->size = sizeof(ibv_flow_spec_tcp_udp);
            flow_spec_udp->val.dst_port = htons(FLOW_UDP_DST_PORT + i);
            flow_spec_udp->mask.dst_port = 0xFFFF;
            // flow_spec_udp->val.src_port = htons(SMARTNS_UDP_MAGIC_PORT + i);
            // flow_spec_udp->mask.src_port = 0xFFFF;
            ibv_flow *flow = ibv_create_flow(qp_handlers_server[i]->qp, flow_attr);
            assert(flow);
            main_flow[i] = flow;
        }

        free(header_buff);
    }

    std::vector<std::thread> threads(FLAGS_threads);
    for (size_t i = 0; i < FLAGS_threads; i++) {
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_nodeType == CLIENT) {
            threads[i] = std::thread(sub_task_client, now_index, qp_handlers_client[i]);
        } else if (FLAGS_nodeType == SERVER) {
            threads[i] = std::thread(sub_task_server, now_index, qp_handlers_server[i]);
        } else {
            init_wr_base_read(*qp_handlers_client[i]);
            init_raw_packet_handler(qp_handlers_server[i], i);
            threads[i] = std::thread(sub_task_relay, now_index, qp_handlers_server[i], qp_handlers_client[i]);
        }
        bind_to_core(threads[i], FLAGS_numaNode, now_index);
    }

    for (size_t i = 0; i < FLAGS_threads; i++) {
        threads[i].join();
    }

    delete[] qp_handlers_client;
    delete[] qp_handlers_server;
    delete[] bufs;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_nodeType < 0 && FLAGS_nodeType > 2) {
        throw std::runtime_error("Invalid nodeType");
    }

    if (FLAGS_payload_size < 64 || static_cast<uint32_t>(FLAGS_payload_size) > PKT_BUF_SIZE) {
        throw std::runtime_error("Invalid payload_size");
    }

    assert(setenv("MLX5_TOTAL_UUARS", "129", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "128", 0) == 0);

    benchmark();

    return 0;
}
