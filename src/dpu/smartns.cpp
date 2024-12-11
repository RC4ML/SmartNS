#include "smartns.h"
#include "rdma_cm/libr.h"
#include "numautil.h"
#include "dma/dma.h"
std::atomic<bool> stop_flag = false;


datapath_manager::datapath_manager(std::string device_name, size_t numa_node) {

    struct ibv_device *ib_dev = ctx_find_dev(device_name.c_str());

    all_rx_context = ctx_open_device(ib_dev);

    struct ibv_port_attr port_attr;
    assert(ibv_query_port(all_rx_context, RDMA_IB_PORT, &port_attr) == 0);
    SMARTNS_INFO("%-20s : %d", "CUR MTU", 128 << (port_attr.active_mtu));

    all_rx_pd = ibv_alloc_pd(all_rx_context);
    assert(all_rx_pd != NULL);

    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        void *send_buf = SmartNS::get_huge_mem(numa_node, SMARTNS_TX_DEPTH * SMARTNS_TX_PACKET_BUFFER);
        for (size_t j = 0;j < SMARTNS_TX_DEPTH * SMARTNS_TX_PACKET_BUFFER / sizeof(size_t);j++) {
            ((size_t *)send_buf)[j] = 0;
        }
        txpath_send_buf_list.push_back(send_buf);

        void *recv_buf = SmartNS::get_huge_mem(numa_node, SMARTNS_RX_DEPTH * SMARTNS_RX_PACKET_BUFFER);
        for (size_t j = 0;j < SMARTNS_RX_DEPTH * SMARTNS_RX_PACKET_BUFFER / sizeof(size_t);j++) {
            ((size_t *)recv_buf)[j] = 0;
        }
        rxpath_recv_buf_list.push_back(recv_buf);
    }

    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        // important
        ibv_context *tx_dma_context = ctx_open_device(ib_dev);
        ibv_pd *tx_dma_pd = ibv_alloc_pd(tx_dma_context);
        assert(tx_dma_pd != NULL);

        txpath_handler *tx_handler = new txpath_handler(tx_dma_context, tx_dma_pd, txpath_send_buf_list[i], SMARTNS_TX_DEPTH * SMARTNS_TX_PACKET_BUFFER);
        rxpath_handler *rx_handler = new rxpath_handler(all_rx_context, all_rx_pd, rxpath_recv_buf_list[i], SMARTNS_RX_DEPTH * SMARTNS_RX_PACKET_BUFFER);
        dma_handler *dma_handler = new ::dma_handler(tx_dma_context, tx_dma_pd, rxpath_recv_buf_list[i], SMARTNS_RX_DEPTH * SMARTNS_RX_PACKET_BUFFER);

        datapath_handler_list.push_back({ dma_handler, tx_handler, rx_handler });
    }
}

datapath_manager::~datapath_manager() {
    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        free(txpath_send_buf_list[i]);
        free(rxpath_recv_buf_list[i]);

        ibv_context *tx_context = datapath_handler_list[i].txpath_handler->context;
        ibv_pd *tx_pd = datapath_handler_list[i].txpath_handler->pd;
        delete datapath_handler_list[i].txpath_handler;
        delete datapath_handler_list[i].rxpath_handler;
        delete datapath_handler_list[i].dma_handler;

        ibv_dealloc_pd(tx_pd);
        ibv_close_device(tx_context);
    }
    ibv_dealloc_pd(all_rx_pd);
    ibv_close_device(all_rx_context);
}


txpath_handler::txpath_handler(ibv_context *context, ibv_pd *pd, void *buf_addr, size_t send_buf_size) {
    this->context = context;
    this->pd = pd;
    tx_depth = SMARTNS_TX_DEPTH;
    send_buf_addr = reinterpret_cast<size_t>(buf_addr);

    num_wrs = SMARTNS_TX_BATCH;
    num_sges_per_wr = SMARTNS_TX_SEG;
    num_sges = num_wrs * num_sges_per_wr;

    ALLOCATE(send_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(send_wr, struct ibv_send_wr, num_wrs);
    ALLOCATE(send_bad_wr, struct ibv_send_wr, 1);

    assert(mr = ibv_reg_mr(pd, buf_addr, send_buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
    assert(send_cq = ibv_create_cq(context, tx_depth, NULL, NULL, 0));

    struct ibv_qp_init_attr tx_qp_init_attr;
    memset(&tx_qp_init_attr, 0, sizeof(tx_qp_init_attr));
    tx_qp_init_attr.send_cq = send_cq;
    tx_qp_init_attr.recv_cq = send_cq;
    tx_qp_init_attr.cap.max_send_wr = tx_depth;
    tx_qp_init_attr.cap.max_send_sge = 2;
    tx_qp_init_attr.qp_type = IBV_QPT_RAW_PACKET;
    assert(send_qp = ibv_create_qp(pd, &tx_qp_init_attr));

    struct ibv_qp_attr tx_qp_attr;
    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_INIT;
    tx_qp_attr.port_num = 1;
    assert(ibv_modify_qp(send_qp, &tx_qp_attr, IBV_QP_STATE | IBV_QP_PORT) == 0);

    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_RTR;
    assert(ibv_modify_qp(send_qp, &tx_qp_attr, IBV_QP_STATE) == 0);

    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_RTS;
    assert(ibv_modify_qp(send_qp, &tx_qp_attr, IBV_QP_STATE) == 0);
}

txpath_handler::~txpath_handler() {
    free(send_sge_list);
    free(send_wr);
    free(send_bad_wr);

    ibv_destroy_qp(send_qp);
    ibv_destroy_cq(send_cq);
    ibv_dereg_mr(mr);
}

rxpath_handler::rxpath_handler(ibv_context *all_rx_context, ibv_pd *all_rx_pd, void *buf_addr, size_t recv_buf_size) {
    context = all_rx_context;
    pd = all_rx_pd;
    rx_depth = SMARTNS_RX_DEPTH;
    recv_buf_addr = reinterpret_cast<size_t>(buf_addr);

    num_wrs = SMARTNS_RX_BATCH;
    num_sges_per_wr = SMARTNS_RX_SEG;
    num_sges = num_wrs * num_sges_per_wr;

    ALLOCATE(recv_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(recv_wr, struct ibv_recv_wr, num_wrs);
    ALLOCATE(recv_bad_wr, struct ibv_recv_wr, 1);

    assert(mr = ibv_reg_mr(pd, buf_addr, recv_buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ));
    assert(recv_cq = ibv_create_cq(context, rx_depth, NULL, NULL, 0));

    // create wq
    ibv_wq_init_attr wq_init_attr;
    memset(&wq_init_attr, 0, sizeof(wq_init_attr));
    wq_init_attr.wq_context = nullptr;
    wq_init_attr.wq_type = IBV_WQT_RQ;
    wq_init_attr.max_wr = rx_depth;
    wq_init_attr.max_sge = SMARTNS_RX_SEG;
    wq_init_attr.pd = pd;
    wq_init_attr.cq = recv_cq;
    wq_init_attr.create_flags = 0;

    assert(recv_wq = ibv_create_wq(context, &wq_init_attr));

    struct ibv_wq_attr wq_attr;
    memset(&wq_attr, 0, sizeof(struct ibv_wq_attr));
    wq_attr.wq_state = IBV_WQS_RDY;
    wq_attr.attr_mask = IBV_WQ_ATTR_STATE;
    assert(ibv_modify_wq(recv_wq, &wq_attr) == 0);
}

rxpath_handler::~rxpath_handler() {
    free(recv_sge_list);
    free(recv_wr);
    free(recv_bad_wr);

    ibv_destroy_wq(recv_wq);
    ibv_destroy_cq(recv_cq);
    ibv_dereg_mr(mr);
}

dma_handler::dma_handler(ibv_context *context, ibv_pd *pd, void *buf_addr, size_t recv_buf_size) {
    this->context = context;
    this->pd = pd;

    assert(rxpath_recv_buf_mr = ibv_reg_mr(pd, buf_addr, recv_buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
        | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_HUGETLB | IBV_ACCESS_RELAXED_ORDERING));
    assert(dma_send_cq = create_dma_cq(context, 64 * SMARTNS_DMA_GROUP_SIZE));

    dma_recv_cq = dma_send_cq;

    for (size_t i = 0;i < SMARTNS_DMA_GROUP_SIZE;i++) {
        ibv_qp *dma_qp = create_dma_qp(context, pd, dma_recv_cq, dma_send_cq, 64);
        init_dma_qp(dma_qp);
        dma_qp_self_connected(dma_qp);

        ibv_qp_ex *dma_qpx = ibv_qp_to_qp_ex(dma_qp);
        mlx5dv_qp_ex *dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(dma_qpx);
        // important for init before do memcpy
        dma_mqpx->wr_memcpy_direct_init(dma_mqpx);
        dma_qp_list.push_back(dma_qp);
        dma_qpx_list.push_back(dma_qpx);
        dma_mqpx_list.push_back(dma_mqpx);
        dma_start_index_list.push_back(0);
        dma_finish_index_list.push_back(0);
    }
}

