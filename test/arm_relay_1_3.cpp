// Client: sudo ./arm_relay_1_3 -deviceName mlx5_0 -batch_size 1 -outstanding 32 -nodeType 0 -threads 2 -payload_size 1024 -serverIp 10.0.0.101
// Relay:  sudo ./arm_relay_1_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 1 -threads 2 -payload_size 1024
// Server: sudo ./arm_relay_1_3 -deviceName mlx5_2 -batch_size 1 -outstanding 32 -nodeType 2 -threads 2 -payload_size 1024

#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"
#include "devx/devx_device.h"
#include "raw_packet/raw_packet.h"

std::atomic<bool> stop_flag = false;
std::mutex IO_LOCK;
static uint64_t NB_RXD = 1024;
static uint64_t NB_TXD = 1024;
static uint64_t PKT_BUF_SIZE = 8448;
static uint64_t PKT_HANDLE_BATCH = 2;
static uint64_t PKT_SEND_OUTSTANDING = 128;

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
unsigned char CLIENT_MAC_ADDR[6] = { 0x02,0xc8,0x55,0x21,0x6d,0xfb };
// bf2 enp3s0f0s0
unsigned char SERVER_MAC_ADDR[6] = { 0x02,0xce,0xf7,0x33,0xe8,0x71 };

void ctrl_c_handler(int) { stop_flag = true; }

void init_raw_packet_handler(qp_handler *handler, size_t thread_index) {
    size_t local_mr_addr = reinterpret_cast<size_t>(handler->buf);
    size_t local_rkey = handler->mr->lkey;

    size_t tx_depth = handler->tx_depth;

    for (int i = 0;i < handler->num_wrs;i++) {
        handler->send_sge_list[i * handler->num_sges_per_wr].lkey = local_rkey;
        handler->send_wr[i].sg_list = handler->send_sge_list + i * handler->num_sges_per_wr;
        handler->send_wr[i].num_sge = handler->num_sges_per_wr;
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
 * @brief Relay 任务
 *
 * 等待 Client 的 DMA 拷贝，然后将数据 Write 到 Server
 *
 * @param[in] thread_index NUMA local index that the thread will be bind to
 */
void sub_task_relay(size_t thread_index, qp_handler *handler, vhca_resource *resource, void **bufs) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(1);
    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler send_server(NB_TXD, PKT_BUF_SIZE, 0);
    offset_handler send_server_comp(NB_TXD, PKT_BUF_SIZE, 0);


    size_t ops = FLAGS_iterations * (BUF_SIZE) / PKT_BUF_SIZE;
    ops = round_up(ops, FLAGS_batch_size);
    ops = round_up(ops, SEND_CQ_BATCH);

    /**
     * DMA 准备
     */
     // MR
    void *local_buffer = bufs[thread_index];
    assert(local_buffer != nullptr);

    uint32_t local_mr_mkey = handler->mr->lkey;
    uint32_t remote_mr_mkey = devx_mr_query_mkey(resource->mr);


    // number of completion
    size_t ne_send_server;

    int done = 0;
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    // size_t tsc_prev = get_tsc();
    // size_t finished_ops = 0;
    while (!done && !stop_flag) {
        // if (thread_index == 0) {
        //     size_t tsc_now = get_tsc();
        //     size_t now_finished_ops = send_server_comp.index();
        //     if (tsc_now - tsc_prev > 5 * 1e7) {
        //         printf("%.2lf\n", 8.0 * (now_finished_ops - finished_ops) * FLAGS_threads * FLAGS_payload_size * get_tsc_freq_per_ns() / (tsc_now - tsc_prev));
        //         tsc_prev = tsc_now;
        //         finished_ops = now_finished_ops;
        //     }
        // }

        ne_send_server = poll_send_cq(*handler, wc_send);
        for (size_t i = 0; i < ne_send_server; i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            // printf("server send comp index %ld\n", send_comp_server.index());
            send_server_comp.step(PKT_HANDLE_BATCH);
        }

        if (send_server.index() < ops && send_server.index() - send_server_comp.index() <= PKT_SEND_OUTSTANDING - PKT_HANDLE_BATCH) {
            for (size_t i = 0;i < PKT_HANDLE_BATCH;i++) {
                handler->send_sge_list[i * handler->num_sges_per_wr].addr = send_server.offset() + reinterpret_cast<size_t>(handler->buf);
                handler->send_sge_list[i * handler->num_sges_per_wr].length = 64;
                handler->send_sge_list[i * handler->num_sges_per_wr].lkey = local_mr_mkey;
                handler->send_sge_list[i * handler->num_sges_per_wr + 1].addr = send_server.offset() + reinterpret_cast<size_t>(resource->addr) + 64;
                handler->send_sge_list[i * handler->num_sges_per_wr + 1].length = FLAGS_payload_size - 64;
                handler->send_sge_list[i * handler->num_sges_per_wr + 1].lkey = remote_mr_mkey;

                handler->send_wr[i].wr_id = send_server.index();
                if (i == PKT_HANDLE_BATCH - 1) {
                    handler->send_wr[i].next = nullptr;
                    handler->send_wr[i].send_flags |= IBV_SEND_SIGNALED;
                }
                send_server.step();
            }
            assert(ibv_post_send(handler->qp, handler->send_wr, &handler->send_bar_wr) == 0);
        }

        if (send_server.index() >= ops) {
            done = 1;
        }

    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_server.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    printf("thread [%ld], duration [%f]s, throughput [%f] Gbps\n", thread_index, duration, speed);

    free(wc_send);
}

/**
 * @brief Client 任务
 *
 * 什么都不做
 *
 * @param[in] thread_index 线程绑定的 NUMA 本地索引
 * @param[in] resource VHCA 资源
 */
void sub_task_client(size_t thread_index, vhca_resource *resource) {
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

/**
 * @brief Host 导出内存区域，供 DPU 执行拷贝
 *
 * roce_init 后得到上下文，从上下文获取：vhca_id 等信息
 * 分配 pd、设置 access_key 等、分配内存
 *
 * 参考 libr:/devx_bench/dma_copy/dma_copy_export.cpp
 *
 * @param[in] rdma_param RDMA 参数
 * @param[in] threads 线程数
 * @param[in] bufs 缓冲区
 * @param[out] vhca_resource VHCA 资源
 */
void host_dma_copy_export(rdma_param &rdma_param, size_t threads, void **bufs, vhca_resource *resources) {
    struct devx_hca_capabilities caps;
    if (devx_query_hca_caps(rdma_param.contexts[0], &caps) != 0) {
        throw std::runtime_error("can't query_hca_caps");
    }

    printf("vhca_id %u\n", caps.vhca_id);
    printf("vhca_resource_manager %u\n", caps.vhca_resource_manager);
    printf("hotplug_manager %u\n", caps.hotplug_manager);
    printf("eswitch_manager %u\n", caps.eswitch_manager);
    printf("introspection_mkey_access_allowed %d\n", caps.introspection_mkey_access_allowed);
    printf("introspection_mkey %u\n", caps.introspection_mkey);
    printf("crossing_vhca_mkey_supported %d\n", caps.crossing_vhca_mkey_supported);
    printf("cross_gvmi_mkey_enabled %d\n", caps.cross_gvmi_mkey_enabled);
    printf("---------------------------\n");

    for (size_t i = 0; i < threads; i++) {
        resources[i].pd = ibv_alloc_pd(rdma_param.contexts[i]);
    }

    uint8_t access_key[32] = { 0 };
    for (size_t i = 0; i < 32; i++) {
        access_key[i] = 1;
    }

    for (size_t i = 0; i < threads; i++) {
        resources[i].vhca_id = caps.vhca_id;
        resources[i].addr = bufs[i];
        resources[i].size = BUF_SIZE;
        resources[i].mr = devx_reg_mr(resources[i].pd, resources[i].addr, resources[i].size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
        if (!resources[i].mr) {
            throw std::runtime_error("can't devx_reg_mr");
        }
        resources[i].mkey = devx_mr_query_mkey(resources[i].mr);
        if (devx_mr_allow_other_vhca_access(resources[i].mr, access_key, sizeof(access_key)) != 0) {
            throw std::runtime_error("can't allow_other_vhca_access");
        }
        printf("mr (umem): thread %ld vhca_id %u addr %p mkey %u\n", i, caps.vhca_id, resources[i].addr, resources[i].mkey);
    }
}

/**
 * @brief DPU 接收内存区域数据
 *
 * DPU 接收 Host 导出的内存区域数据，检查参数是否合法，并创建 PD、MR
 *
 * 参考 libr:/devx_bench/dma_copy/dma_copy_bench.cpp
 *
 * @param[in] rdma_param RDMA 参数
 * @param[in] threads 线程数
 * @param[out] resources VHCA 资源
 */
void dpu_dma_copy_check(rdma_param &rdma_param, qp_handler **handlers, size_t threads, void **bufs, vhca_resource *resources) {
    uint32_t mmo_dma_max_length = get_mmo_dma_max_length(rdma_param.contexts[0]);
    fprintf(stderr, "mmo_dma_max_length %u\n", mmo_dma_max_length);
    rt_assert(mmo_dma_max_length >= static_cast<uint32_t>(FLAGS_payload_size));

    rt_assert(FLAGS_outstanding >= static_cast<uint32_t>(FLAGS_batch_size));
    rt_assert(1ul * FLAGS_batch_size * FLAGS_payload_size <= resources[0].size);
    uint8_t access_key[32] = { 0 };
    for (int i = 0; i < 32; i++) {
        access_key[i] = 1;
    }

    for (size_t i = 0; i < threads; i++) {
        resources[i].pd = handlers[i]->pd;
        if (resources[i].pd == nullptr) {
            fprintf(stderr, "ibv_alloc_pd failed\n");
            throw std::runtime_error("ibv_alloc_pd failed");
        }
        resources[i].mr = devx_create_crossing_mr(resources[i].pd, resources[i].addr, resources[i].size, resources[i].vhca_id, resources[i].mkey, access_key, sizeof(access_key));
        if (resources[i].mr == nullptr) {
            fprintf(stderr, "devx_create_crossing_mr failed\n");
            throw std::runtime_error("devx_create_crossing_mr failed");
        }
    }
}

/**
 * @brief Client（Host）与 Relay（DPU）交换 DMA 信息
 *
 * 先初始化 Roce，完成资源分配，再交换信息。
 *
 * @param[in] net_param 网络参数
 * @param[in] rdma_param RDMA 参数
 *
 */
vhca_resource *connect_peer_dma_client(tcp_param &net_param, rdma_param &rdma_param, void **bufs) {
    size_t dummy;
    (void)dummy;

    vhca_resource *resources = new vhca_resource[FLAGS_threads];
    socket_init(net_param);

    host_dma_copy_export(rdma_param, FLAGS_threads, bufs, resources);
    dummy = write(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource) * FLAGS_threads);

    return resources;
}

vhca_resource *connect_peer_dma_relay(tcp_param &net_param, rdma_param &rdma_param, qp_handler **handlers, void **bufs) {
    size_t dummy;
    (void)dummy;

    vhca_resource *resources = new vhca_resource[FLAGS_threads];
    socket_init(net_param);

    dummy = read(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource) * FLAGS_threads);
    dpu_dma_copy_check(rdma_param, handlers, FLAGS_threads, bufs, resources);

    return resources;
}

void benchmark() {
    qp_handler **qp_handlers_server = nullptr;
    ibv_flow **main_flow = nullptr;
    vhca_resource *resources = nullptr;

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
    rdma_param.sge_per_wr = 2;
    roce_init(rdma_param, FLAGS_threads);

    tcp_param net_param;
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;

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

    if (FLAGS_nodeType == CLIENT) // client
    {
        net_param.isServer = false;
        resources = connect_peer_dma_client(net_param, rdma_param, bufs);
        socket_close(net_param);
    } else if (FLAGS_nodeType == RELAY) // relay
    {
        net_param.isServer = true;
        resources = connect_peer_dma_relay(net_param, rdma_param, qp_handlers_server, bufs);
        socket_close(net_param);
    }

    std::vector<std::thread> threads(FLAGS_threads);
    for (size_t i = 0; i < FLAGS_threads; i++) {
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_nodeType == CLIENT) {
            threads[i] = std::thread(sub_task_client, now_index, resources + i);
        } else if (FLAGS_nodeType == RELAY) {
            init_raw_packet_handler(qp_handlers_server[i], i);
            threads[i] = std::thread(sub_task_relay, now_index, qp_handlers_server[i], resources + i, bufs);
        } else if (FLAGS_nodeType == SERVER) {
            threads[i] = std::thread(sub_task_server, now_index, qp_handlers_server[i]);
        }
        bind_to_core(threads[i], FLAGS_numaNode, now_index);
    }

    for (size_t i = 0; i < FLAGS_threads; i++) {
        threads[i].join();
    }

    delete[] qp_handlers_server;
    delete[] bufs;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(setenv("MLX5_TOTAL_UUARS", "17", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "16", 0) == 0);

    benchmark();

    return 0;
}
