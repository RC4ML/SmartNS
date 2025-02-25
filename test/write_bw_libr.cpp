#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"

//  ./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 64 -payload_size 1024 -serverIp 10.0.0.200 -iterations 10000
//  ./write_bw -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 80 -payload_size 1024 -is_server -iterations 10000

std::atomic<bool> stop_flag = false;
size_t base_alloc_size = 16 * 1024 * 1024;

void ctrl_c_handler(int) { stop_flag = true; }

void sub_task_client(size_t thread_index, qp_handler *handler) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(2);

    size_t send_recv_buf_size = base_alloc_size / 2;
    offset_handler send(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);

    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    size_t tx_depth = FLAGS_outstanding;//handler->tx_depth;

    size_t ops = FLAGS_iterations * (send_recv_buf_size) / FLAGS_payload_size;
    ops = round_up(ops, FLAGS_batch_size);
    ops = round_up(ops, SEND_CQ_BATCH);

    for (size_t i = 0;i < tx_depth;i++) {
        post_send_batch(*handler, 1, send, FLAGS_payload_size);
    }

    size_t ne_send;

    size_t batch_size = FLAGS_batch_size;
    int done = 0;
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    while (!done && !stop_flag) {
        ne_send = poll_send_cq(*handler, wc_send);
        for (size_t i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            // printf("send comp index %ld\n", send_comp.index());
            // send_comp.step(SEND_CQ_BATCH);
            send_comp.step(1);
        }

        if (send.index() < ops && send.index() - send_comp.index() <= tx_depth - batch_size) {
            size_t now_send_num = std::min(ops - send.index(), batch_size);
            post_send_batch(*handler, now_send_num, send, FLAGS_payload_size);
        }

        if (send.index() >= ops) {
            done = 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    printf("thread [%ld], duration [%f]s, throughput [%f] Gpbs\n", thread_index, duration, speed);

    free(wc_send);
    sleep(1);
}

void sub_task_server(size_t thread_index, qp_handler *handler) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    while (!stop_flag) {
        sleep(1);
    }
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
    rdma_param.batch_size = FLAGS_batch_size;
    rdma_param.sge_per_wr = 1;
    roce_init(rdma_param, FLAGS_threads);

    pingpong_info *info = new pingpong_info[2 * FLAGS_threads]();

    void **bufs = new void *[FLAGS_threads];
    qp_handler **qp_handlers = new qp_handler * [FLAGS_threads]();

    for (size_t i = 0;i < FLAGS_threads;i++) {
        bufs[i] = get_huge_mem(FLAGS_numaNode, base_alloc_size);
        for (size_t j = 0;j < base_alloc_size / (sizeof(int));j++) {
            (reinterpret_cast<int **> (bufs))[i][j] = 0;
        }
    }

    for (size_t i = 0;i < FLAGS_threads;i++) {
        qp_handlers[i] = create_qp_rc(rdma_param, bufs[i], base_alloc_size, info + i, i);
    }
    exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

    for (size_t i = 0;i < FLAGS_threads;i++) {
        connect_qp_rc(rdma_param, *qp_handlers[i], info + i + FLAGS_threads, info + i);
        init_wr_base_write(*qp_handlers[i]);
    }

    std::vector<std::thread> threads(FLAGS_threads);
    for (size_t i = 0;i < FLAGS_threads;i++) {
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_is_server) {
            threads[i] = std::thread(sub_task_server, now_index, qp_handlers[i]);
        } else {
            threads[i] = std::thread(sub_task_client, now_index, qp_handlers[i]);
        }
        bind_to_core(threads[i], FLAGS_numaNode, now_index);
    }

    for (size_t i = 0;i < FLAGS_threads;i++) {
        threads[i].join();
    }

    for (size_t i = 0;i < FLAGS_threads;i++) {
        free(qp_handlers[i]->send_sge_list);
        free(qp_handlers[i]->recv_sge_list);
        free(qp_handlers[i]->send_wr);
        free(qp_handlers[i]->recv_wr);

        ibv_destroy_qp(qp_handlers[i]->qp);
        ibv_destroy_cq(qp_handlers[i]->send_cq);
        ibv_destroy_cq(qp_handlers[i]->recv_cq);
        ibv_dereg_mr(qp_handlers[i]->mr);

        if (i == 0) {
            ibv_dealloc_pd(qp_handlers[0]->pd);
            ibv_close_device(rdma_param.contexts[0]);
        }
        free_huge_mem(reinterpret_cast<void *>(qp_handlers[i]->buf));
        free(qp_handlers[i]);
    }

    delete[]qp_handlers;
    delete[]bufs;
    delete[]info;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(setenv("MLX5_TOTAL_UUARS", "129", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "128", 0) == 0);

    benchmark();

    return 0;
}