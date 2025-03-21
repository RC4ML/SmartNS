#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "numautil.h"
#include "hdr_histogram.h"

using namespace std;
std::mutex IO_LOCK;

// client: sudo ./snap_bench  -threads 1  -serverIp 10.0.0.200  -iterations 100000 -numPack 51200 -payload_size 1024
// server: sudo ./snap_bench  -threads 1 -is_server  -iterations 100000 -numPack 51200 -payload_size 1024

std::atomic<bool> stop_flag = false;
std::atomic<double> total_bw = 0;
int BUF_SIZE;
size_t BATCH_SIZE = 1;
size_t OUTSTANDING = 64;

hdr_histogram *latency_hist = nullptr;
double scale_value = 10;

void ctrl_c_handler(int) { stop_flag = true; }

DEFINE_int32(numPack, 1024, "numPack");
DEFINE_bool(recordLatency, false, "recordLatency");

const auto kPageSize = 4096;
struct ListNode {
    ListNode *next;
    std::byte padding[kPageSize];
};

void sub_task_server(int thread_index, qp_handler *handler, size_t ops) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    int ne_recv;
    struct ibv_wc *wc_recv = NULL;
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler recv(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);
    offset_handler recv_comp(FLAGS_numPack, FLAGS_payload_size, BUF_SIZE / 2);

    size_t rx_depth = handler->rx_depth;

    for (size_t i = 0; i < min(size_t(rx_depth), ops);i++) {
        post_recv(*handler, recv.offset(), FLAGS_payload_size);
        recv.step();
    }
    int done = 0;


    void *copy_bufs = malloc(BUF_SIZE);
    for (int i = 0;i < BUF_SIZE;i++) {
        static_cast<char *>(copy_bufs)[i] = i;
    }

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

    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    printf("Server thread[%d] start\n", thread_index);
    while (!done && !stop_flag) {
        ne_recv = poll_recv_cq(*handler, wc_recv);
        if (ne_recv != 0 && begin_time.tv_sec == 0) {
            clock_gettime(CLOCK_MONOTONIC, &begin_time);
        }

        for (int i = 0;i < ne_recv;i++) {
            if (recv.index() < ops) {
                post_recv(*handler, recv.offset(), FLAGS_payload_size);
                recv.step();
            }
            assert(wc_recv[i].status == IBV_WC_SUCCESS);
            assert(wc_recv[i].byte_len == static_cast<uint32_t>(FLAGS_payload_size));
            recv_comp.step();

            // fake freeflow
            {
                memcpy(reinterpret_cast<void *>(reinterpret_cast<uint64_t>(copy_bufs) + wc_recv[i].wr_id), reinterpret_cast<void *>(handler->buf + wc_recv[i].wr_id), wc_recv[i].byte_len);
                memcpy(reinterpret_cast<void *>(reinterpret_cast<uint64_t>(copy_bufs) + BUF_SIZE / 2), wc_recv + i, sizeof(struct ibv_wc));
                size_t now_traverse_num = traverse_num;
                while (now_traverse_num > 0) {
                    now_head = now_head->next;
                    now_traverse_num--;
                }
            }
        }

        if (recv_comp.index() >= ops) {
            done = 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * recv_comp.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    std::lock_guard<std::mutex> guard(IO_LOCK);
    total_bw = total_bw + speed;
    printf("Data verification success, thread [%d], duration [%f]s, throughput [%f] Gpbs\n", thread_index, duration, speed);

    free(wc_recv);
    free(copy_bufs);
}

void sub_task_client(int thread_index, qp_handler *handler, size_t ops) {
    sleep(2);
    // assert(latency_hist);
    wait_scheduling(FLAGS_numaNode, thread_index);

    std::vector<size_t>timers(128);
    size_t timer_head = 0, timer_tail = 0;
    int ne_send;
    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    offset_handler send(FLAGS_numPack, FLAGS_payload_size, 0);
    offset_handler send_comp(FLAGS_numPack, FLAGS_payload_size, 0);

    size_t tx_depth = OUTSTANDING;//handler->tx_depth;

    for (size_t i = 0; i < min(tx_depth, ops);i++) {
        post_send(*handler, send.offset(), FLAGS_payload_size);
        timers[timer_head] = get_tsc();
        timer_head = (timer_head + 1) % 128;
        send.step();
    }
    struct timespec begin_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    while (send_comp.index() < ops && !stop_flag) {
        ne_send = poll_send_cq(*handler, wc_send);

        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            if (thread_index == 0 && FLAGS_recordLatency) {
                hdr_record_value_atomic(latency_hist, (get_tsc() - timers[timer_tail]) * 10);
                timer_tail = (timer_tail + 1) % 128;
            }
            send_comp.step();
        }
        if (send.index() < ops && send.index() - send_comp.index() < tx_depth) {
            post_send(*handler, send.offset(), FLAGS_payload_size);
            timers[timer_head] = get_tsc();
            timer_head = (timer_head + 1) % 128;
            send.step();
        }
    }
    while (send_comp.index() < send.index()) {
        ne_send = poll_send_cq(*handler, wc_send);
        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            if (thread_index == 0 && FLAGS_recordLatency) {
                hdr_record_value_atomic(latency_hist, (get_tsc() - timers[timer_tail]) * 10);
                timer_tail = (timer_tail + 1) % 128;
            }
            send_comp.step();
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_comp.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    std::lock_guard<std::mutex> guard(IO_LOCK);
    total_bw = total_bw + speed;
    printf("Data verification success, thread [%d], duration [%f]s, throughput [%f] Gpbs", thread_index, duration, speed);

    free(wc_send);
}

void benchmark() {
    tcp_param net_param;
    net_param.isServer = FLAGS_is_server;
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;
    socket_init(net_param);

    rdma_param rdma_param;
    rdma_param.device_name = FLAGS_deviceName;
    rdma_param.numa_node = FLAGS_numaNode;
    rdma_param.batch_size = BATCH_SIZE;
    rdma_param.sge_per_wr = 1;
    roce_init(rdma_param, FLAGS_threads);


    BUF_SIZE = FLAGS_numPack * FLAGS_payload_size * 2;
    size_t ops = size_t(1) * FLAGS_iterations * FLAGS_numPack;
    printf("OPS : [%ld]", ops);

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
        init_wr_base_write(*qp_handlers[i]);
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
    printf("Total bandwidth: %f Gbps\n", total_bw.load());
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


    if (!FLAGS_is_server && FLAGS_recordLatency) {
        hdr_init(1000, 50000000, 3, &latency_hist);
    }
    benchmark();
    if (!FLAGS_is_server != 0 && FLAGS_recordLatency) {
        hdr_percentiles_print(latency_hist, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);
        hdr_close(latency_hist);
    }
}