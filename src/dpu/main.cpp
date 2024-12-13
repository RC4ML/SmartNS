#include "smartns.h"
#include "gflags_common.h"
#include "tcp_cm/tcp_cm.h"
#include "rdma_cm/libr.h"

void ctrl_c_handler(int) { stop_flag = true; }

void client_datapath(datapath_handler *handler) {
    SmartNS::wait_scheduling(FLAGS_numaNode, handler->cpu_id);
    if (handler->thread_id != 0) {
        return;
    }
    for (size_t i = 0;i < SMARTNS_TX_BATCH;i++) {
        handler->txpath_handler->send_sge_list[i * SMARTNS_TX_SEG].addr = handler->txpath_handler->send_offset_handler.offset() + handler->txpath_handler->send_buf_addr;
        handler->txpath_handler->send_sge_list[i * SMARTNS_TX_SEG].length = SMARTNS_TX_PACKET_BUFFER;
        handler->txpath_handler->send_wr[i].num_sge = 1;
        handler->txpath_handler->send_wr[i].sg_list = handler->txpath_handler->send_sge_list + i * SMARTNS_TX_SEG;
        handler->txpath_handler->send_wr[i].wr_id = handler->txpath_handler->send_offset_handler.offset() + handler->txpath_handler->send_buf_addr;
        handler->txpath_handler->send_wr[i].send_flags = IBV_SEND_SIGNALED;
        if (i > 0) {
            handler->txpath_handler->send_wr[i - 1].next = &handler->txpath_handler->send_wr[i];
        }
        handler->txpath_handler->send_offset_handler.step();
    }
    assert(ibv_post_send(handler->txpath_handler->send_qp, handler->txpath_handler->send_wr, &handler->txpath_handler->send_bad_wr) == 0);

    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    while (!stop_flag) {
        int ne_send = ibv_poll_cq(handler->txpath_handler->send_cq, CTX_POLL_BATCH, wc_send);
        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            printf("thread %ld send comp %u\n", handler->thread_id, wc_send[i].byte_len);
        }
    }
}

void server_datapath(datapath_handler *handler) {
    SmartNS::wait_scheduling(FLAGS_numaNode, handler->cpu_id);

    struct ibv_wc *wc_recv = NULL;
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);
    while (!stop_flag) {
        int ne_recv = ibv_poll_cq(handler->rxpath_handler->recv_cq, CTX_POLL_BATCH, wc_recv);
        for (int i = 0;i < ne_recv;i++) {
            assert(wc_recv[i].status == IBV_WC_SUCCESS);
            printf("thread %ld recv %u\n", handler->thread_id, wc_recv[i].byte_len);
        }
    }
}

void host_controlpath(controlpath_manager *control_manager) {
    SmartNS::wait_scheduling(FLAGS_numaNode, control_manager->cpu_id);

    socket_init(control_manager->control_net_param);
    exchange_data(control_manager->control_net_param, reinterpret_cast<char *>(&control_manager->local_bf_info), reinterpret_cast<char *>(&control_manager->remote_host_info), sizeof(pingpong_info));

    connect_qp_rc(control_manager->control_rdma_param, *control_manager->control_qp_handler, &control_manager->remote_host_info, &control_manager->local_bf_info);

    init_wr_base_send_recv(*control_manager->control_qp_handler);

    for (size_t i = 0;i < control_manager->control_rx_depth;i++) {
        post_recv(*control_manager->control_qp_handler, control_manager->recv_handler.offset(), control_manager->control_packet_size);
        control_manager->recv_handler.step();
    }

    struct ibv_wc *wc_recv = NULL;
    struct ibv_wc *wc_send = NULL;
    int ne_recv;

    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    while (!stop_flag) {
        ne_recv = poll_recv_cq(*control_manager->control_qp_handler, wc_recv);
        for (int i = 0;i < ne_recv;i++) {
            assert(wc_recv[i].status == IBV_WC_SUCCESS);
            printf("recv %u\n", wc_recv[i].byte_len);

            control_manager->recv_comp_handler.step();

            post_recv(*control_manager->control_qp_handler, control_manager->recv_handler.offset(), control_manager->control_packet_size);
            control_manager->recv_handler.step();
        }
    }

    free(wc_send);
    free(wc_recv);
}

int main(int argc, char *argv[]) {

    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(SMARTNS_TX_RX_CORE + SMARTNS_CONTROL_CORE <= SmartNS::num_lcores_per_numa_node());

    datapath_manager *data_manager = new datapath_manager(FLAGS_deviceName, FLAGS_numaNode, FLAGS_is_server);
    controlpath_manager *control_manager = new controlpath_manager(FLAGS_deviceName, FLAGS_numaNode, FLAGS_is_server);

    size_t num_threads = SMARTNS_TX_RX_CORE + SMARTNS_CONTROL_CORE;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    printf("is server %d\n", FLAGS_is_server);

    size_t now_cpu_id = 0;
    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        data_manager->datapath_handler_list[i].cpu_id = now_cpu_id;
        if (FLAGS_is_server) {
            threads.emplace_back(std::thread(server_datapath, &data_manager->datapath_handler_list[i]));
        } else {
            threads.emplace_back(std::thread(client_datapath, &data_manager->datapath_handler_list[i]));
        }
        SmartNS::bind_to_core(threads[i], FLAGS_numaNode, now_cpu_id);
        now_cpu_id++;
    }

    control_manager->cpu_id = now_cpu_id;
    threads.emplace_back(std::thread(host_controlpath, control_manager));
    SmartNS::bind_to_core(threads[SMARTNS_TX_RX_CORE], FLAGS_numaNode, now_cpu_id);

    now_cpu_id++;

    for (size_t i = 0;i < num_threads;i++) {
        threads[i].join();
    }

    delete data_manager;
    delete control_manager;
    exit(0);
}