#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "numautil.h"
#include "Crc32.h"

using namespace std;
std::mutex IO_LOCK;

int BATCH_SIZE = 1;
int OUTSTANDING = 32;
int BUF_SIZE;

std::atomic<bool> stop_flag = false;
std::atomic<double> total_bw = 0;

DEFINE_uint32(type, 0, "Test Type");
DEFINE_int32(numPack, 1024, "numPack");

enum TestType {
    CPU = 0,
    CPU_CRCOffload = 1,
    CPU_CRCOffload_DSA = 2,
};

const auto kPageSize = 4096;
struct ListNode {
    ListNode *next;
    std::byte padding[kPageSize];
};

void ctrl_c_handler(int) { stop_flag = true; }

int SOLAR_HEADER_SIZE = 64;

std::vector<std::vector<uint64_t>>dsa_submit_tsc;

constexpr size_t dsa_submit_cost_ns = 100;
constexpr size_t dsa_finish_cost_ns = 4000;

inline uint32_t do_solar_handler(size_t thread_id, void *buf, size_t pkt_size, void *dst_buf, size_t recv_counter, offset_handler &recv_comp) {
    if (FLAGS_type == TestType::CPU) {
        uint32_t crc = crc32_fast(buf, pkt_size);
        memcpy(dst_buf, buf, pkt_size);
        recv_comp.step();
        return crc;
    } else if (FLAGS_type == TestType::CPU_CRCOffload) {
        memcpy(dst_buf, buf, pkt_size);
        recv_comp.step();
        return 0;
    } else if (FLAGS_type == TestType::CPU_CRCOffload_DSA) {
        size_t now_tsc = get_tsc();
        while (get_tsc() < now_tsc + get_tsc_freq_per_ns() * dsa_submit_cost_ns) {
        }
        dsa_submit_tsc[thread_id][recv_counter % OUTSTANDING] = get_tsc();
        return 0;
    }
    return 0;
}

inline void do_solar_extra_handler(size_t thread_id, size_t recv_counter, offset_handler &recv_comp) {
    if (FLAGS_type == TestType::CPU) {
        return;
    } else if (FLAGS_type == TestType::CPU_CRCOffload) {
        return;
    } else if (FLAGS_type == TestType::CPU_CRCOffload_DSA) {
        const uint64_t finish_delta_tsc = dsa_finish_cost_ns * get_tsc_freq_per_ns();
        while (recv_comp.index() < recv_counter) {
            const uint64_t now_tsc = get_tsc();
            const uint64_t submit_tsc = dsa_submit_tsc[thread_id][recv_comp.index() % OUTSTANDING];
            if (now_tsc < submit_tsc || (now_tsc - submit_tsc) < finish_delta_tsc) {
                break;
            }
            recv_comp.step();
        }
        return;
    }
}

void sub_task_server(int thread_index, qp_handler *handler, size_t ops) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    int ne_send;
    int ne_recv;
    struct ibv_wc *wc_recv = NULL;
    struct ibv_wc *wc_send = NULL;

    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler send(FLAGS_numPack, FLAGS_payload_size, 0);
    offset_handler send_comp(FLAGS_numPack, FLAGS_payload_size, 0);
    offset_handler recv(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);
    offset_handler recv_comp(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);

    size_t tx_depth = OUTSTANDING;
    size_t rx_depth = handler->rx_depth;

    void *copy_bufs = malloc(BUF_SIZE);
    for (int i = 0;i < BUF_SIZE;i++) {
        static_cast<char *>(copy_bufs)[i] = i;
    }

    for (size_t i = 0; i < min(size_t(rx_depth), ops);i++) {
        post_recv(*handler, recv.offset(), FLAGS_payload_size);
        recv.step();
    }
    int done = 0;
    uint32_t crc_sum = 0;
    size_t recv_counter = 0;

    size_t num_nodes = 128 * 1024 * 1024 / (64);
    std::vector<ListNode> list(num_nodes);
    for (size_t i = 0;i < list.size() / 2;i++) {
        list[i].next = &list[list.size() - 1 - i];
    }
    for (size_t i = list.size() / 2 + 1;i < list.size();i++) {
        list[i].next = &list[list.size() - i];
    }
    list[list.size() / 2].next = &list[0];

    ListNode *now_head = &list[0];
    size_t traverse_num = 8;


    printf("Server thread[%d] start\n", thread_index);
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;
    while (!done && !stop_flag) {
        ne_recv = poll_recv_cq(*handler, wc_recv);
        if (ne_recv != 0 && begin_time.tv_sec == 0) {
            clock_gettime(CLOCK_MONOTONIC, &begin_time);
        }

        for (int i = 0;i < ne_recv;i++) {
            assert(wc_recv[i].status == IBV_WC_SUCCESS);
            assert(wc_recv[i].byte_len == static_cast<uint32_t>(FLAGS_payload_size));
            {
                size_t now_traverse_num = traverse_num;
                while (now_traverse_num > 0) {
                    now_head = now_head->next;
                    now_traverse_num--;
                }
            }

            crc_sum += do_solar_handler(thread_index, reinterpret_cast<void *>(handler->buf + wc_recv[i].wr_id), FLAGS_payload_size, reinterpret_cast<void *>(reinterpret_cast<uint64_t>(copy_bufs) + wc_recv[i].wr_id), recv_counter, recv_comp);
            recv_counter++;
            if (recv.index() < ops) {
                post_recv(*handler, recv.offset(), FLAGS_payload_size);
                recv.step();
            }
        }

        do_solar_extra_handler(thread_index, recv_counter, recv_comp);

        while (send.index() < recv_comp.index() && (send.index() - send_comp.index()) < tx_depth) {
            post_send(*handler, send.offset(), 8);
            send.step();
        }
        ne_send = poll_send_cq(*handler, wc_send);
        for (int i = 0;i < ne_send;i++) {
            if (wc_send[i].status != IBV_WC_SUCCESS) {
                printf("Thread : %d, wc_send[%ld].status = %d, i=%d\n", thread_index, send_comp.index(), wc_send[i].status, i);
            }
            send_comp.step();
        }
        if (recv_comp.index() >= ops && send_comp.index() >= ops) {
            done = 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * recv_comp.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    std::lock_guard<std::mutex> guard(IO_LOCK);
    total_bw = total_bw + speed;
    printf("Data verification success, thread [%d], duration [%f]s, throughput [%f] Gpbs, tmp %ld\n", thread_index, duration, speed, crc_sum + reinterpret_cast<uint64_t>(now_head));

    free(wc_recv);
}

void sub_task_client(int thread_index, qp_handler *handler, size_t ops) {
    sleep(8);
    wait_scheduling(FLAGS_numaNode, thread_index);

    int ne_send;
    int ne_recv;
    struct ibv_wc *wc_send = NULL;
    struct ibv_wc *wc_recv = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler send(FLAGS_numPack, FLAGS_payload_size, 0);
    offset_handler send_comp(FLAGS_numPack, FLAGS_payload_size, 0);
    offset_handler recv(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);
    offset_handler recv_comp(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);

    size_t tx_depth = OUTSTANDING;//handler->tx_depth;
    size_t rx_depth = handler->rx_depth;


    for (size_t i = 0;i < min(rx_depth, ops);i++) {
        post_recv(*handler, recv.offset(), FLAGS_payload_size);
        recv.step();
    }

    for (size_t i = 0; i < min(tx_depth, ops);i++) {
        post_send(*handler, send.offset(), FLAGS_payload_size);
        send.step();
    }

    struct timespec begin_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    while ((send_comp.index() < ops || recv_comp.index() < ops) && !stop_flag) {
        ne_send = poll_send_cq(*handler, wc_send);

        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            send_comp.step();
        }
        if (send.index() < ops && send.index() - recv_comp.index() < tx_depth) {
            post_send(*handler, send.offset(), FLAGS_payload_size);
            send.step();
        }
        ne_recv = poll_recv_cq(*handler, wc_recv);
        for (int i = 0;i < ne_recv;i++) {
            if (recv.index() < ops) {
                post_recv(*handler, recv.offset(), FLAGS_payload_size);
                recv.step();
            }
            assert(wc_recv[i].status == IBV_WC_SUCCESS);

            if (wc_recv[i].byte_len != static_cast<uint32_t>(8)) {
                printf("Client thread[%d] verify length failed, index:[%ld], byte_len:[%d]", thread_index, recv_comp.index(), wc_recv[i].byte_len);
            }
            recv_comp.step();
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_comp.index()  / 1000 / 1000 / duration;

    std::lock_guard<std::mutex> guard(IO_LOCK);
    total_bw = total_bw + speed;
    printf("Data verification success, thread [%d], duration [%f]s, throughput [%f] Mops\n", thread_index, duration, speed);

    free(wc_send);
}

void benchmark() {
    tcp_param net_param;
    net_param.isServer = FLAGS_is_server;
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;
    socket_init(net_param);
    total_bw.store(0);

    rdma_param rdma_param;
    rdma_param.device_name = FLAGS_deviceName;
    rdma_param.numa_node = FLAGS_numaNode;
    rdma_param.batch_size = BATCH_SIZE;
    rdma_param.sge_per_wr = 1;
    roce_init(rdma_param, FLAGS_threads);

    BUF_SIZE = FLAGS_numPack * FLAGS_payload_size * 2;
    size_t ops = size_t(1) * FLAGS_iterations * FLAGS_numPack;
    printf("OPS : [%ld]\n", ops);

    pingpong_info *info = new pingpong_info[2 * FLAGS_threads]();
    void **bufs = new void *[FLAGS_threads];
    qp_handler **qp_handlers = new qp_handler * [FLAGS_threads]();
    for (size_t i = 0;i < FLAGS_threads;i++) {
        bufs[i] = get_huge_mem(FLAGS_numaNode, BUF_SIZE);
        for (int j = 0;j < BUF_SIZE / static_cast<int>(sizeof(int));j++) {
            (reinterpret_cast<int **>(bufs))[i][j] = j;
        }
    }

    for (size_t i = 0;i < FLAGS_threads;i++) {
        qp_handlers[i] = create_qp_rc(rdma_param, bufs[i], BUF_SIZE, info + i, i);
    }
    exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

    for (size_t i = 0;i < FLAGS_threads;i++) {
        connect_qp_rc(rdma_param, *qp_handlers[i], info + i + FLAGS_threads, info + i);
        init_wr_base_send_recv(*qp_handlers[i]);
    }
    dsa_submit_tsc.resize(FLAGS_threads);
    for (size_t i = 0;i < FLAGS_threads;i++) {
        dsa_submit_tsc[i].resize(OUTSTANDING);
    }

    vector<thread> threads(FLAGS_threads);
    for (size_t i = 0;i < FLAGS_threads;i++) {
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_is_server) {
            threads[i] = thread(sub_task_server, now_index, qp_handlers[i], ops);
        } else {
            threads[i] = thread(sub_task_client, now_index, qp_handlers[i], ops);
        }
        bind_to_core(threads[i], FLAGS_numaNode, now_index);
    }
    for (size_t i = 0;i < FLAGS_threads;i++) {
        threads[i].join();
    }
    double final_speed = total_bw.load();
    printf("Total Speed: %f Mops\n", final_speed);
    if (FLAGS_is_server) {
        printf("RESULT|experiment=4|method=solar_bench|role=server|payload_size=%lu|threads=%lu|type=%u|total_gbps=%.6f\n",
            static_cast<unsigned long>(FLAGS_payload_size),
            static_cast<unsigned long>(FLAGS_threads),
            FLAGS_type,
            final_speed);
    } else {
        printf("RESULT|experiment=4|method=solar_bench|role=client|payload_size=%lu|threads=%lu|type=%u|total_mops=%.6f\n",
            static_cast<unsigned long>(FLAGS_payload_size),
            static_cast<unsigned long>(FLAGS_threads),
            FLAGS_type,
            final_speed);
    }
    for (size_t i = 0;i < FLAGS_threads;i++) {
        free(qp_handlers[i]->send_sge_list);
        free(qp_handlers[i]->recv_sge_list);
        free(qp_handlers[i]->send_wr);
        free(qp_handlers[i]->recv_wr);

        ibv_destroy_qp(qp_handlers[i]->qp);
        ibv_dereg_mr(qp_handlers[i]->mr);
        ibv_destroy_cq(qp_handlers[i]->send_cq);
        ibv_destroy_cq(qp_handlers[i]->recv_cq);
        ibv_dealloc_pd(qp_handlers[i]->pd);

        ibv_close_device(rdma_param.contexts[i]);
        free(qp_handlers[i]);
    }

    delete[]info;
    delete[]bufs;
    delete[]qp_handlers;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    printf("Test Type : [%d]\n", FLAGS_type);
    if (FLAGS_type > TestType::CPU_CRCOffload_DSA) {
        printf("Invalid test type");
        return -1;
    }
    benchmark();
}
