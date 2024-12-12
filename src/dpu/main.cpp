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

int main(int argc, char *argv[]) {

    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(SMARTNS_TX_RX_CORE + SMARTNS_CONTROL_CORE <= SmartNS::num_lcores_per_numa_node());

    datapath_manager *data_manager = new datapath_manager(FLAGS_deviceName, FLAGS_numaNode, FLAGS_is_server);

    size_t num_threads = SMARTNS_TX_RX_CORE;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    printf("is server %d\n", FLAGS_is_server);
    for (size_t i = 0;i < num_threads;i++) {
        data_manager->datapath_handler_list[i].cpu_id = i;
        if (FLAGS_is_server) {
            threads.emplace_back(std::thread(server_datapath, &data_manager->datapath_handler_list[i]));
        } else {
            threads.emplace_back(std::thread(client_datapath, &data_manager->datapath_handler_list[i]));
        }
        SmartNS::bind_to_core(threads[i], FLAGS_numaNode, i);
    }

    for (size_t i = 0;i < num_threads;i++) {
        threads[i].join();
    }

    delete data_manager;
    exit(0);
}