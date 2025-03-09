// Client: ./arm_relay_1_2 -deviceName mlx5_0 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -serverIp 10.0.0.101
// Relay:  ./arm_relay_1_2 -deviceName mlx5_2 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server -serverIp 10.0.0.201
// Server: ./arm_relay_1_2 -deviceName mlx5_2 -batch_size 1 -threads 1 -outstanding 32 -payload_size 1024 -is_server

#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "hdr_histogram.h"
#include "numautil.h"
#include "devx/devx_device.h"

std::atomic<bool> stop_flag = false;
size_t base_alloc_size = 16 * 1024 * 1024;
/**
 * 本程序的数据量：
 *
 * - base_alloc_size: 16MB，`buf` 为每个线程分配一个 16MB 的缓冲区
 * - send_recv_buf_size: 8MB，
 * - FLAGS_batch_size: 用户指定
 * - FLAGS_payload_size: 用户给定，在 post_send* 中给到 ibv_sge.length
 *
 */

void ctrl_c_handler(int) { stop_flag = true; }

/**
 * @brief Relay 任务
 *
 * 等待 Client 的 DMA 拷贝，然后将数据 Write 到 Server
 *
 * @param[in] thread_index NUMA local index that the thread will be bind to
 */
void sub_task_relay(size_t thread_index, qp_handler *qp_handler, vhca_resource *resource, void **bufs)
{
    wait_scheduling(FLAGS_numaNode, thread_index);

    sleep(2);

    size_t send_recv_buf_size = base_alloc_size / 2;
    offset_handler send_client(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp_client(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_server(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp_server(send_recv_buf_size / FLAGS_payload_size, FLAGS_payload_size, 0);

    size_t tx_depth = FLAGS_outstanding;
    size_t ops = FLAGS_iterations * (send_recv_buf_size) / FLAGS_payload_size;
    ops = round_up(ops, FLAGS_batch_size);
    ops = round_up(ops, SEND_CQ_BATCH);

    /**
     * DMA 准备
     */
    // MR
    void *local_buffer = bufs[thread_index];
    assert(local_buffer != nullptr);
    ibv_mr *local_mr = ibv_reg_mr(resource->pd, local_buffer, base_alloc_size,
                                  IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_HUGETLB | IBV_ACCESS_RELAXED_ORDERING);
    if (!local_mr)
    {
        fprintf(stderr, "can't create local_mr\n");
        exit(__LINE__);
    }
    // CQ
    ibv_cq *sq_cq = create_dma_cq(resource->pd->context, tx_depth);
    ibv_cq *rq_cq = create_dma_cq(resource->pd->context, tx_depth);
    // QP
    ibv_qp *qp = create_dma_qp(resource->pd->context, resource->pd, rq_cq, sq_cq, FLAGS_outstanding);
    init_dma_qp(qp);
    dma_qp_self_connected(qp);
    ibv_qp_ex *dma_qpx = ibv_qp_to_qp_ex(qp);
    mlx5dv_qp_ex *dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(dma_qpx);
    dma_mqpx->wr_memcpy_direct_init(dma_mqpx);
    // mkey
    uint32_t local_mr_mkey = local_mr->lkey;
    uint32_t remote_mr_mkey = devx_mr_query_mkey(resource->mr);
    uint64_t dma_start_index = 0, dma_finish_index = 0;

    /**
     * @brief RDMA WC 准备
     *
     */
    struct ibv_wc *wc_send_client = NULL;
    struct ibv_wc *wc_send_server = NULL;
    ALLOCATE(wc_send_client, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_send_server, struct ibv_wc, CTX_POLL_BATCH);

    /**
     * DMA 发送
     */
    for (size_t i = 0; i < tx_depth; i++)
    {
        dma_qpx->wr_id = dma_start_index;
        dma_qpx->wr_flags = IBV_SEND_SIGNALED;
        // DMA Read
        dma_mqpx->wr_memcpy_direct(dma_mqpx, local_mr_mkey, (uint64_t)local_buffer + send_client.offset(), remote_mr_mkey, (uint64_t)resource->addr + send_client.offset(), FLAGS_payload_size);
        send_client.step(1);
    }

    // number of completion
    size_t ne_send_client, ne_send_server;

    size_t batch_size = FLAGS_batch_size;
    int done = 0;
    struct timespec begin_time, end_time;
    begin_time.tv_nsec = 0;
    begin_time.tv_sec = 0;

    clock_gettime(CLOCK_MONOTONIC, &begin_time);
    struct timespec last_print_time = begin_time;
    size_t last_client_index = 0;
    while (!done && !stop_flag)
    {
        // DMA 轮询
        ne_send_client = ibv_poll_cq(sq_cq, CTX_POLL_BATCH, wc_send_client);
        for (size_t i = 0; i < ne_send_client; i++)
        {
            assert(wc_send_client[i].status == IBV_WC_SUCCESS);
            // printf("Client: %6ld Poll CQ\n", send_comp_client.index());
            send_comp_client.step(1);
        }

        // DMA 发送
        auto outstanding_num = send_client.index() - send_comp_client.index();
        auto now_send_num = tx_depth - outstanding_num;
        if (send_client.index() < ops && now_send_num > 0)
        {
            for (size_t i = 0; i < now_send_num; i++)
            {
                // printf("Client: %6ld Post WR\n", send_client.index());
                dma_qpx->wr_id = dma_start_index;
                dma_qpx->wr_flags = IBV_SEND_SIGNALED;
                // DMA Read
                dma_mqpx->wr_memcpy_direct(dma_mqpx, local_mr_mkey, (uint64_t)local_buffer + send_client.offset(), remote_mr_mkey, (uint64_t)resource->addr + send_client.offset(), FLAGS_payload_size);
                send_client.step(1);
            }
        }

        // RDMA 轮询
        ne_send_server = poll_send_cq(*qp_handler, wc_send_server);
        for (size_t i = 0; i < ne_send_server; i++)
        {
            assert(wc_send_server[i].status == IBV_WC_SUCCESS);
            // printf("Server: %6ld Poll CQ\n", send_comp_server.index());
            send_comp_server.step(1);
        }

        // RDMA 发送
        // 未发送完，且
        // 当前发送的 index 小于 DMA 完成的 index 且
        // 正在传输的（outstanding）小于 tx_depth - batch_size
        outstanding_num = send_server.index() - send_comp_server.index();
        now_send_num = std::min(tx_depth - outstanding_num, send_comp_client.index() - send_server.index());
        if (send_server.index() < ops && now_send_num > 0)
        {
            for (size_t i = 0; i < now_send_num; i++)
            {
                // printf("Server: %6ld Post WR\n", send_server.index());
                // RDMA write to server
                post_send_batch(*qp_handler, 1, send_server, FLAGS_payload_size);
                // post_send_batch will step handler
                // send_server.step(1);
            }
        }

        // 结束条件
        if (send_client.index() >= ops && send_comp_client.index() >= ops && send_server.index() >= ops && send_comp_server.index() >= ops)
        {
            done = 1;
        }

        // Print throughput statistics every second
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_since_print = (current_time.tv_sec - last_print_time.tv_sec) + 
                                    (current_time.tv_nsec - last_print_time.tv_nsec) / 1e9;

        if (elapsed_since_print >= 1.0) {  // Print every second
            double instant_speed = 8.0 * (send_client.index() - last_client_index) * 
                                  FLAGS_payload_size / elapsed_since_print / 1000 / 1000 / 1000;
            
            double elasped_since_begin = (current_time.tv_sec - begin_time.tv_sec) + 
                                        (current_time.tv_nsec - begin_time.tv_nsec) / 1e9;
            
            printf("%ld\t%f\t%f\n", thread_index, elasped_since_begin, instant_speed);
            
            // Update tracking variables
            last_print_time = current_time;
            last_client_index = send_client.index();
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double duration = (end_time.tv_sec - begin_time.tv_sec) + (end_time.tv_nsec - begin_time.tv_nsec) / 1e9;
    double speed = 8.0 * send_client.index() * FLAGS_payload_size / 1000 / 1000 / 1000 / duration;

    printf("thread [%ld], duration [%f]s, throughput [%f] Gbps\n", thread_index, duration, speed);

    free(wc_send_client);
    free(wc_send_server);
    sleep(1);
}

/**
 * @brief Client 任务
 *
 * 什么都不做
 *
 * @param[in] thread_index 线程绑定的 NUMA 本地索引
 * @param[in] resource VHCA 资源
 */
void sub_task_client(size_t thread_index, vhca_resource *resource)
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
void host_dma_copy_export(rdma_param &rdma_param, size_t threads, void **bufs, vhca_resource *resources)
{
    struct devx_hca_capabilities caps;
    if (devx_query_hca_caps(rdma_param.contexts[0], &caps) != 0)
    {
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

    for (size_t i = 0; i < threads; i++)
    {
        resources[i].pd = ibv_alloc_pd(rdma_param.contexts[i]);
    }

    uint8_t access_key[32] = {0};
    for (size_t i = 0; i < 32; i++)
    {
        access_key[i] = 1;
    }

    for (size_t i = 0; i < threads; i++)
    {
        resources[i].vhca_id = caps.vhca_id;
        resources[i].addr = bufs[i];
        resources[i].size = base_alloc_size;
        resources[i].mr = devx_reg_mr(resources[i].pd, resources[i].addr, resources[i].size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
        if (!resources[i].mr)
        {
            throw std::runtime_error("can't devx_reg_mr");
        }
        resources[i].mkey = devx_mr_query_mkey(resources[i].mr);
        if (devx_mr_allow_other_vhca_access(resources[i].mr, access_key, sizeof(access_key)) != 0)
        {
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
void dpu_dma_copy_check(rdma_param &rdma_param, size_t threads, void **bufs, vhca_resource *resources)
{
    uint32_t mmo_dma_max_length = get_mmo_dma_max_length(rdma_param.contexts[0]);
    fprintf(stderr, "mmo_dma_max_length %u\n", mmo_dma_max_length);
    rt_assert(mmo_dma_max_length >= static_cast<uint32_t>(FLAGS_payload_size));

    rt_assert(FLAGS_outstanding >= static_cast<uint32_t>(FLAGS_batch_size));
    rt_assert(1ul * FLAGS_batch_size * FLAGS_payload_size <= resources[0].size);
    uint8_t access_key[32] = {0};
    for (int i = 0; i < 32; i++)
    {
        access_key[i] = 1;
    }

    for (size_t i = 0; i < threads; i++)
    {
        resources[i].pd = ibv_alloc_pd(rdma_param.contexts[i]);
        if (resources[i].pd == nullptr)
        {
            fprintf(stderr, "ibv_alloc_pd failed\n");
            throw std::runtime_error("ibv_alloc_pd failed");
        }
        resources[i].mr = devx_create_crossing_mr(resources[i].pd, resources[i].addr, resources[i].size, resources[i].vhca_id, resources[i].mkey, access_key, sizeof(access_key));
        if (resources[i].mr == nullptr)
        {
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
vhca_resource *connect_peer_dma_client(tcp_param &net_param, rdma_param &rdma_param, void **bufs)
{
    vhca_resource *resources = new vhca_resource[FLAGS_threads];
    roce_init(rdma_param, FLAGS_threads);
    socket_init(net_param);

    host_dma_copy_export(rdma_param, FLAGS_threads, bufs, resources);
    write(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource) * FLAGS_threads);

    return resources;
}

vhca_resource *connect_peer_dma_relay(tcp_param &net_param, rdma_param &rdma_param, void **bufs)
{
    vhca_resource *resources = new vhca_resource[FLAGS_threads];
    roce_init(rdma_param, FLAGS_threads);
    socket_init(net_param);

    read(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource) * FLAGS_threads);
    dpu_dma_copy_check(rdma_param, FLAGS_threads, bufs, resources);

    return resources;
}

/**
 * @brief 两个 RDMA 之间交换信息
 *
 * 初始化 RDMA 资源，创建 QP，并交换信息。
 *
 * @param[in] net_param 网络参数
 * @param[in] bufs 缓冲区
 * @param[in] rdma_param RDMA 参数
 * @return qp_handler** 创建的 QP 处理器数组
 */
qp_handler **connect_peer_rdma(tcp_param &net_param, void **bufs, rdma_param &rdma_param)
{
    roce_init(rdma_param, FLAGS_threads);

    pingpong_info *info = new pingpong_info[2 * FLAGS_threads]();
    qp_handler **qp_handlers = new qp_handler *[FLAGS_threads];
    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        qp_handlers[i] = create_qp_rc(rdma_param, bufs[i], base_alloc_size, info + i, i);
    }
    exchange_data(net_param, reinterpret_cast<char *>(info), reinterpret_cast<char *>(info) + sizeof(pingpong_info) * FLAGS_threads, sizeof(pingpong_info) * FLAGS_threads);

    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        connect_qp_rc(rdma_param, *qp_handlers[i], info + i + FLAGS_threads, info + i);
        init_wr_base_write(*qp_handlers[i]);
    }

    delete[] info;

    return qp_handlers;
}

void benchmark()
{
    void **bufs = new void *[FLAGS_threads];
    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        bufs[i] = get_huge_mem(FLAGS_numaNode, base_alloc_size);
        for (size_t j = 0; j < base_alloc_size / (sizeof(int)); j++)
        {
            (reinterpret_cast<int **>(bufs))[i][j] = 0;
        }
    }

    tcp_param net_param;
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;
    /**
     * Relay 和 Server 之间建立 RDMA 连接：
     */
    rdma_param rdma_param_rdma;
    rdma_param_rdma.device_name = FLAGS_deviceName;
    rdma_param_rdma.numa_node = FLAGS_numaNode;
    rdma_param_rdma.batch_size = FLAGS_batch_size;
    rdma_param_rdma.sge_per_wr = 1;
    qp_handler **qp_handlers = nullptr;
    if (FLAGS_is_server) // Relay or Server
    {
        net_param.isServer = FLAGS_serverIp.empty();
        socket_init(net_param);
        qp_handlers = connect_peer_rdma(net_param, bufs, rdma_param_rdma);
        if (qp_handlers == nullptr)
        {
            throw std::runtime_error("qp_handlers is nullptr");
        }
        socket_close(net_param);
    }
    /**
     * Relay 和 Client 之间建立 DMA 连接：
     *
     */
    rdma_param rdma_param_dma;
    rdma_param_dma.device_name = FLAGS_deviceName;
    rdma_param_dma.numa_node = FLAGS_numaNode;
    rdma_param_dma.batch_size = FLAGS_batch_size;
    rdma_param_dma.sge_per_wr = 1;
    vhca_resource *resources = nullptr;
    if (!FLAGS_is_server) // client
    {
        net_param.isServer = false;
        resources = connect_peer_dma_client(net_param, rdma_param_dma, bufs);
        socket_close(net_param);
    }
    else if (!FLAGS_serverIp.empty()) // relay
    {
        net_param.isServer = true;
        resources = connect_peer_dma_relay(net_param, rdma_param_dma, bufs);
        socket_close(net_param);
    }

    std::vector<std::thread> threads(FLAGS_threads);
    // print begin time UTC
    char time_str[100];
    time_t now = time(NULL);
    strftime(time_str, 100, "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("begin time: %s\n", time_str);
    printf("Thread\tSecond(s)\tThroughput(Gbps)\n");
    for (size_t i = 0; i < FLAGS_threads; i++)
    {
        // <<< DEBUG
        fprintf(stderr, "FLAGS_threads %ld\n", FLAGS_threads);
        // >>> DEBUG
        size_t now_index = i + FLAGS_coreOffset;
        if (FLAGS_is_server)
        {
            if (FLAGS_serverIp.empty()) // Server
            {
                threads[i] = std::thread(sub_task_server, now_index, qp_handlers[i]);
            }
            else // Relay
            {
                threads[i] = std::thread(sub_task_relay, now_index, qp_handlers[i], resources + i, bufs);
            }
        }
        else // Client
        {
            threads[i] = std::thread(sub_task_client, now_index, resources + i);
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

    if (qp_handlers != nullptr)
    {
        delete[] qp_handlers;
    }
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
