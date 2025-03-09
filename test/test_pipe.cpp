#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"

//  ./test_pipe -deviceName mlx5_0 -batch_size 1 -serverIp 10.0.0.200
//  ./test_pipe -deviceName mlx5_0 -batch_size 1 -is_server

std::atomic<bool> stop_flag = false;
size_t base_alloc_size = 16 * 1024 * 1024;
hdr_histogram *latency_hist = nullptr;

void ctrl_c_handler(int) { stop_flag = true; }

void sub_task_client(size_t thread_index, qp_handler *handler) {

    wait_scheduling(FLAGS_numaNode, thread_index);

    while (!stop_flag) {
        sleep(1);
    }
}

void sub_task_server(size_t thread_index, qp_handler *handler) {
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(2);


    hdr_init(1000, 50000000, 3, &latency_hist);


    size_t send_recv_buf_size = base_alloc_size / 2;
    offset_handler send(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);

    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    size_t ops = 1000;

    handler->send_wr->opcode = IBV_WR_DRIVER1;

    size_t tsc_time;
    while (ops-- && !stop_flag) {
        smartns_post_send(*handler, send.offset(), FLAGS_payload_size);
        tsc_time = get_tsc();
        while (!stop_flag) {
            int ne_send = smartns_poll_send_cq(*handler, wc_send);
            if (ne_send > 0) {
                hdr_record_value_atomic(latency_hist, (get_tsc() - tsc_time) * 10);
                assert(wc_send[0].status == IBV_WC_SUCCESS);
                break;
            }
        }
    }
    free(wc_send);

    hdr_percentiles_print(latency_hist, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);
    hdr_close(latency_hist);

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
    smartns_roce_init(rdma_param, 1);

    pingpong_info *info = new pingpong_info[2 * FLAGS_threads]();

    void **bufs = new void *[FLAGS_threads];
    qp_handler **qp_handlers = new qp_handler * [FLAGS_threads]();

    for (size_t i = 0;i < FLAGS_threads;i++) {
        bufs[i] = get_huge_mem(FLAGS_numaNode, base_alloc_size);
        for (size_t j = 0;j < base_alloc_size / (sizeof(int));j++) {
            (reinterpret_cast<int **>(bufs))[i][j] = 0;
        }
    }

    for (size_t i = 0;i < FLAGS_threads;i++) {
        qp_handlers[i] = smartns_create_qp_rc(rdma_param, bufs[i], base_alloc_size, info + i, 0, i);
    }
    exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

    for (size_t i = 0;i < FLAGS_threads;i++) {
        smartns_connect_qp_rc(rdma_param, *qp_handlers[i], info + i + FLAGS_threads, info + i);
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

    // for (size_t i = 0;i < FLAGS_threads;i++) {
    //     free(qp_handlers[i]->send_sge_list);
    //     free(qp_handlers[i]->recv_sge_list);
    //     free(qp_handlers[i]->send_wr);
    //     free(qp_handlers[i]->recv_wr);

    //     smartns_destroy_qp(qp_handlers[i]->qp);
    //     smartns_destroy_cq(qp_handlers[i]->send_cq);
    //     smartns_destroy_cq(qp_handlers[i]->recv_cq);
    //     smartns_dereg_mr(qp_handlers[i]->mr);

    //     if (i == 0) {
    //         smartns_dealloc_pd(qp_handlers[0]->pd);
    //         smartns_close_device(rdma_param.contexts[0]);
    //     }
    //     free_huge_mem(reinterpret_cast<void *>(qp_handlers[i]->buf));
    //     free(qp_handlers[i]);
    // }

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