#include "smartns.h"
#include "gflags_common.h"
#include "tcp_cm/tcp_cm.h"
#include "rdma_cm/libr.h"

std::atomic<bool> stop_flag = false;

void ctrl_c_handler(int) { stop_flag = true; }

void client_datapath(datapath_handler *handler) {
    SmartNS::wait_scheduling(FLAGS_numaNode, handler->cpu_id);
    if (handler->thread_id != 0) {
        return;
    }

    struct ibv_wc *wc_send = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);

    while (!stop_flag) {
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

        int ne_send = ibv_poll_cq(handler->txpath_handler->send_cq, CTX_POLL_BATCH, wc_send);
        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            printf("thread %ld send comp %u\n", handler->thread_id, wc_send[i].byte_len);
            handler->txpath_handler->send_comp_offset_handler.step();
        }

        sleep(1);
    }

    return;
}

void server_datapath(datapath_handler *handler) {
    SmartNS::wait_scheduling(FLAGS_numaNode, handler->cpu_id);

    while (!stop_flag) {
        handler->handle_recv();
    }
    return;
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
    int ne_recv, ne_send;

    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    while (!stop_flag) {
        ne_recv = poll_recv_cq(*control_manager->control_qp_handler, wc_recv);
        for (int i = 0;i < ne_recv;i++) {
            assert(wc_recv[i].status == IBV_WC_SUCCESS);

            SMARTNS_KERNEL_COMMON_PARAMS *common_param = reinterpret_cast<SMARTNS_KERNEL_COMMON_PARAMS *>(wc_recv[i].wr_id + control_manager->control_qp_handler->buf);

            if (_IOC_TYPE(common_param->cmd) != SMARTNS_IOCTL) {
                SMARTNS_ERROR("invalid ioctl type %d\n", _IOC_TYPE(common_param->cmd));
                exit(1);
            }
            switch (common_param->cmd) {
            case SMARTNS_IOC_OPEN_DEVICE:
                control_manager->handle_open_device(reinterpret_cast<SMARTNS_OPEN_DEVICE_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_CLOSE_DEVICE:
                control_manager->handle_close_device(reinterpret_cast<SMARTNS_CLOSE_DEVICE_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_ALLOC_PD:
                control_manager->handle_alloc_pd(reinterpret_cast<SMARTNS_ALLOC_PD_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_DEALLOC_PD:
                control_manager->handle_dealloc_pd(reinterpret_cast<SMARTNS_DEALLOC_PD_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_REG_MR:
                control_manager->handle_reg_mr(reinterpret_cast<SMARTNS_REG_MR_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_DESTROY_MR:
                control_manager->handle_destory_mr(reinterpret_cast<SMARTNS_DESTROY_MR_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_CREATE_CQ:
                control_manager->handle_create_cq(reinterpret_cast<SMARTNS_CREATE_CQ_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_DESTROY_CQ:
                control_manager->handle_destory_cq(reinterpret_cast<SMARTNS_DESTROY_CQ_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_CREATE_QP:
                control_manager->handle_create_qp(reinterpret_cast<SMARTNS_CREATE_QP_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_DESTROY_QP:
                control_manager->handle_destory_qp(reinterpret_cast<SMARTNS_DESTROY_QP_PARAMS *>(common_param));
                break;
            case SMARTNS_IOC_MODIFY_QP:
                control_manager->handle_modify_qp(reinterpret_cast<SMARTNS_MODIFY_QP_PARAMS *>(common_param));
                break;
            default:
                SMARTNS_ERROR("invalid ioctl cmd %d\n", common_param->cmd);
                exit(1);
            }
            void *send_buf = reinterpret_cast<void *>(control_manager->send_handler.offset() + control_manager->control_qp_handler->buf);
            memcpy(send_buf, common_param, _IOC_SIZE(common_param->cmd));

            post_send(*control_manager->control_qp_handler, control_manager->send_handler.offset(), _IOC_SIZE(common_param->cmd));
            control_manager->send_handler.step();

            control_manager->recv_comp_handler.step();
            post_recv(*control_manager->control_qp_handler, control_manager->recv_handler.offset(), control_manager->control_packet_size);
            control_manager->recv_handler.step();
        }

        ne_send = poll_send_cq(*control_manager->control_qp_handler, wc_send);
        for (int i = 0;i < ne_send;i++) {
            assert(wc_send[i].status == IBV_WC_SUCCESS);
            control_manager->send_comp_handler.step();
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

    assert(setenv("MLX5_TOTAL_UUARS", "129", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "128", 0) == 0);

    controlpath_manager *control_manager = new controlpath_manager(FLAGS_deviceName, FLAGS_numaNode, FLAGS_is_server);

    datapath_manager *data_manager = new datapath_manager(control_manager->control_rdma_param.contexts[0], control_manager->control_qp_handler->pd, FLAGS_numaNode, FLAGS_is_server);

    // add datamanager to control manager for qp initial
    control_manager->data_manager = data_manager;

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
    if (FLAGS_is_server) {
        threads.emplace_back(std::thread(host_controlpath, control_manager));
        SmartNS::bind_to_core(threads[SMARTNS_TX_RX_CORE], FLAGS_numaNode, now_cpu_id);
    }

    now_cpu_id++;

    for (size_t i = 0;i < threads.size();i++) {
        threads[i].join();
    }

    delete data_manager;
    delete control_manager;
    exit(0);
}