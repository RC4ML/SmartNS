// Client: ./arm_relay_1_1 -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -serverIp 10.0.0.101
// Relay:  ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server -serverIp 10.0.0.201
// Server: ./arm_relay_1_1 -deviceName mlx5_2 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server

#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"

std::atomic<bool> stop_flag = false;
size_t base_alloc_size = 16 * 1024 * 1024;

void ctrl_c_handler(int) { stop_flag = true; }

/**
 * @brief Relay thread
 *
 * - Fetch data from client
 * - Send data to server
 *
 * @param[in] thread_index NUMA local index that the thread will be bind to
 */
void sub_task_relay(size_t thread_index, qp_handler *qp_handler_server, qp_handler *qp_handler_client)
{
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(2);

    size_t send_recv_buf_size = base_alloc_size / 2;
    offset_handler send_client(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp_client(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_server(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp_server(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);

    struct ibv_wc *wc_send_client = NULL;
    struct ibv_wc *wc_send_server = NULL;
    ALLOCATE(wc_send_client, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_send_server, struct ibv_wc, CTX_POLL_BATCH);

    size_t tx_depth = FLAGS_outstanding;
    size_t ops = FLAGS_iterations * (send_recv_buf_size) / FLAGS_payload_size;
    ops = round_up(ops, FLAGS_batch_size);
    ops = round_up(ops, SEND_CQ_BATCH);

    for (size_t i = 0; i < tx_depth; i++)
    {
        // RDMA read from client
        post_send_batch(*qp_handler_client, 1, send_client, FLAGS_payload_size);
    }

    size_t ne_send_client, ne_send_server;

    size_t batch_size = FLAGS_batch_size;
    int done = 0;
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    while (!done && !stop_flag)
    {
        ne_send_client = poll_send_cq(*qp_handler_client, wc_send_client);
        for (size_t i = 0; i < ne_send_client; i++)
        {
            // printf("wc status %s\n", ibv_wc_status_str(wc_send_client[i].status));
            assert(wc_send_client[i].status == IBV_WC_SUCCESS);
            // printf("client send comp index %ld\n", send_comp_client.index());
            send_comp_client.step(1);
            // RDMA write to server
            post_send_batch(*qp_handler_server, 1, send_server, FLAGS_payload_size);
        }

        ne_send_server = poll_send_cq(*qp_handler_server, wc_send_server);
        for (size_t i = 0; i < ne_send_server; i++)
        {
            assert(wc_send_server[i].status == IBV_WC_SUCCESS);
            // printf("server send comp index %ld\n", send_comp_server.index());
            send_comp_server.step(1);
        }

        if (send_client.index() < ops && send_client.index() - send_comp_client.index() <= tx_depth - batch_size)
        {
            size_t now_send_num = std::min(ops - send_client.index(), batch_size);
            post_send_batch(*qp_handler_client, now_send_num, send_client, FLAGS_payload_size);
        }

        if (send_client.index() >= ops && send_comp_client.index() >= ops)
        {
            done = 1;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_client.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    printf("thread [%ld], duration [%f]s, throughput [%f] Gpbs\n", thread_index, duration, speed);

    free(wc_send_client);
    free(wc_send_server);
    sleep(1);
}

/**
 * @brief Client thread
 *
 * - Do nothing
 */
void sub_task_client(size_t thread_index, qp_handler *qp_handler)
{
    wait_scheduling(FLAGS_numaNode, thread_index);

    while (!stop_flag)
    {
        sleep(1);
    }
}

/**
 * @brief Server thread
 *
 * - Do nothing
 */
void sub_task_server(size_t thread_index, qp_handler *qp_handler)
{
    wait_scheduling(FLAGS_numaNode, thread_index);

    while (!stop_flag)
    {
        sleep(1);
    }
}

void benchmark()
{
    qp_handler **qp_handlers_client = nullptr;
    qp_handler **qp_handlers_server = nullptr;
    void **bufs = new void *[FLAGS_threads];
    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        bufs[i] = get_huge_mem(FLAGS_numaNode, base_alloc_size);
        for (size_t j = 0; j < base_alloc_size / (sizeof(int)); j++)
        {
            (reinterpret_cast<int **>(bufs))[i][j] = 0;
        }
    }

    // setup RC QP
    if (FLAGS_is_server)
    {
        tcp_param net_param;
        net_param.isServer = true;
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
        qp_handlers_server = new qp_handler *[FLAGS_threads];
        for (size_t i = 0; i < FLAGS_threads; i++)
        {
            qp_handlers_server[i] = create_qp_rc(rdma_param, bufs[i], base_alloc_size, info + i, i);
        }
        exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

        for (size_t i = 0; i < FLAGS_threads; i++)
        {
            connect_qp_rc(rdma_param, *qp_handlers_server[i], info + i + FLAGS_threads, info + i);
        }

        delete[] info;
        socket_close(net_param);
    }
    if (!FLAGS_serverIp.empty())
    {
        tcp_param net_param;
        net_param.isServer = false;
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
        qp_handlers_client = new qp_handler *[FLAGS_threads];
        for (size_t i = 0; i < FLAGS_threads; i++)
        {
            qp_handlers_client[i] = create_qp_rc(rdma_param, bufs[i], base_alloc_size, info + i, i);
        }
        exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

        for (size_t i = 0; i < FLAGS_threads; i++)
        {
            connect_qp_rc(rdma_param, *qp_handlers_client[i], info + i + FLAGS_threads, info + i);
        }

        delete[] info;
        socket_close(net_param);
    }

    if (qp_handlers_client == nullptr && qp_handlers_server == nullptr)
    {
        throw std::runtime_error("qp_handlers_client and qp_handlers_server are both nullptr");
    }

    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        if (FLAGS_is_server)
        {
            if (FLAGS_serverIp.empty()) // server
            {
                init_wr_base_write(*qp_handlers_server[i]);
            }
            else // relay
            {
                init_wr_base_read(*qp_handlers_client[i]);
                init_wr_base_write(*qp_handlers_server[i]);
            }
        }
        else // client
        {
            init_wr_base_read(*qp_handlers_client[i]);
        }
    }

    std::vector<std::thread> threads(FLAGS_threads);
    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_is_server)
        {
            if (FLAGS_serverIp.empty())
            {
                threads[i] = std::thread(sub_task_server, now_index, qp_handlers_server[i]);
            }
            else
            {
                threads[i] = std::thread(sub_task_relay, now_index, qp_handlers_server[i], qp_handlers_client[i]);
            }
        }
        else
        {
            threads[i] = std::thread(sub_task_client, now_index, qp_handlers_client[i]);
        }
        bind_to_core(threads[i], FLAGS_numaNode, now_index);
    }

    for (size_t i = 0; i < FLAGS_threads; i++)
    {
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

    delete[] qp_handlers_client;
    delete[] qp_handlers_server;
    delete[] bufs;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(setenv("MLX5_TOTAL_UUARS", "129", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "128", 0) == 0);

    benchmark();

    return 0;
}
