#include "smartns.h"
#include "numautil.h"
#include "dma/dma.h"
#include "rxe/rxe.h"
#include "raw_packet/raw_packet.h"

void datapath_manager::create_raw_packet_main_qp() {
    size_t rxq_size = SMARTNS_TX_RX_CORE;
    if (!is_log2(rxq_size)) {
        struct ibv_device_attr_ex attr;
        assert(ibv_query_device_ex(global_context, 0, &attr) == 0);
        rxq_size = attr.rss_caps.max_rwq_indirection_table_size;
        if (rxq_size < SMARTNS_TX_RX_CORE) {
            SMARTNS_ERROR("RSS table size is too small\n");
            exit(1);
        }
    }
    ibv_wq **wqs = new ibv_wq * [rxq_size];
    for (size_t i = 0;i < rxq_size;i++) {
        wqs[i] = datapath_handler_list[i % SMARTNS_TX_RX_CORE].rxpath_handler->recv_wq;
    }

    assert(is_log2(rxq_size));

    main_rss_size = rxq_size;

    ibv_rwq_ind_table_init_attr rwq_ind_table_init_attr;
    rwq_ind_table_init_attr.log_ind_tbl_size = std::log2(rxq_size);
    rwq_ind_table_init_attr.ind_tbl = wqs;
    rwq_ind_table_init_attr.comp_mask = 0;

    assert(main_rwq_ind_table = ibv_create_rwq_ind_table(global_context, &rwq_ind_table_init_attr));

    delete[]wqs;

    assert(main_rss_qp == nullptr && main_flow == nullptr);
    assert(global_context != nullptr && global_pd != nullptr);

    struct ibv_qp_init_attr_ex qp_init_attr_ex;
    memset(&qp_init_attr_ex, 0, sizeof(qp_init_attr_ex));

    qp_init_attr_ex.qp_context = nullptr;
    qp_init_attr_ex.srq = nullptr;
    qp_init_attr_ex.cap.max_inline_data = 0;
    qp_init_attr_ex.qp_type = IBV_QPT_RAW_PACKET;
    qp_init_attr_ex.pd = global_pd;
    qp_init_attr_ex.create_flags = 0; //ibv_qp_create_flags 
    qp_init_attr_ex.rwq_ind_tbl = main_rwq_ind_table;
    qp_init_attr_ex.rx_hash_conf.rx_hash_function = IBV_RX_HASH_FUNC_TOEPLITZ;
    qp_init_attr_ex.rx_hash_conf.rx_hash_key_len = RSS_HASH_KEY_LENGTH;
    qp_init_attr_ex.rx_hash_conf.rx_hash_key = RSS_KEY;
    qp_init_attr_ex.rx_hash_conf.rx_hash_fields_mask = IBV_RX_HASH_SRC_IPV4 | IBV_RX_HASH_DST_IPV4 |
        IBV_RX_HASH_SRC_PORT_UDP | IBV_RX_HASH_DST_PORT_UDP;

    qp_init_attr_ex.comp_mask = IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_IND_TABLE | IBV_QP_INIT_ATTR_RX_HASH;

    assert(main_rss_qp = ibv_create_qp_ex(global_context, &qp_init_attr_ex));
}

void datapath_manager::create_main_flow() {
    assert(main_flow == nullptr);
    assert(main_rss_qp != nullptr && global_context != nullptr && global_context != nullptr);

    size_t flow_attr_total_size = sizeof(ibv_flow_attr) + sizeof(ibv_flow_spec_eth) + sizeof(ibv_flow_spec_tcp_udp);

    void *header_buff = malloc(flow_attr_total_size);
    memset(header_buff, 0, flow_attr_total_size);
    ibv_flow_attr *flow_attr = reinterpret_cast<ibv_flow_attr *>(header_buff);
    ibv_flow_spec_eth *flow_spec_eth = reinterpret_cast<ibv_flow_spec_eth *>(flow_attr + 1);
    ibv_flow_spec_tcp_udp *flow_spec_udp = reinterpret_cast<ibv_flow_spec_tcp_udp *>(flow_spec_eth + 1);
    flow_attr->size = flow_attr_total_size;
    flow_attr->priority = 0;
    flow_attr->num_of_specs = 2;
    flow_attr->port = RDMA_IB_PORT;
    flow_attr->flags = 0;
    flow_attr->type = IBV_FLOW_ATTR_NORMAL;

    flow_spec_eth->type = IBV_FLOW_SPEC_ETH;
    flow_spec_eth->size = sizeof(ibv_flow_spec_eth);
    flow_spec_eth->val.ether_type = htons(0x0800);
    flow_spec_eth->mask.ether_type = 0xffff;
    if (is_server) {
        memcpy(flow_spec_eth->val.dst_mac, server_mac, 6);
    } else {
        memcpy(flow_spec_eth->val.dst_mac, client_mac, 6);
    }
    memset(flow_spec_eth->mask.dst_mac, 0xFF, 6);

    flow_spec_udp->type = IBV_FLOW_SPEC_UDP;
    flow_spec_udp->size = sizeof(ibv_flow_spec_tcp_udp);
    flow_spec_udp->val.dst_port = htons(SMARTNS_UDP_MAGIC_PORT);
    flow_spec_udp->mask.dst_port = 0xFFFF;

    assert(main_flow = ibv_create_flow(main_rss_qp, flow_attr));
    free(header_buff);
}

datapath_manager::datapath_manager(ibv_context *all_context, ibv_pd *all_pd, size_t numa_node, bool is_server) {
    this->numa_node = numa_node;
    this->is_server = is_server;

    global_context = all_context;
    global_pd = all_pd;

    struct ibv_port_attr port_attr;
    assert(ibv_query_port(global_context, RDMA_IB_PORT, &port_attr) == 0);
    SMARTNS_INFO("%-20s : %d", "CUR MTU", 128 << (port_attr.active_mtu));

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

        txpath_handler *tx_handler = new txpath_handler(global_context, global_pd, txpath_send_buf_list[i], SMARTNS_TX_DEPTH * SMARTNS_TX_PACKET_BUFFER);
        rxpath_handler *rx_handler = new rxpath_handler(global_context, global_pd, rxpath_recv_buf_list[i], SMARTNS_RX_DEPTH * SMARTNS_RX_PACKET_BUFFER);
        dma_handler *dma_handler = new ::dma_handler(global_context, global_pd);
        struct ibv_wc *wc_send_recv = new ibv_wc[CTX_POLL_BATCH];
        datapath_handler_list.push_back({ i,i, dma_handler, tx_handler, rx_handler,wc_send_recv });
    }

    create_raw_packet_main_qp();
    create_main_flow();


    // init tx path, include udp src port and init send buffer
    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        ipv4_tuple v4_tuple;
        v4_tuple.src_addr = is_server ? ip_to_uint32(server_ip) : ip_to_uint32(client_ip);
        v4_tuple.dst_addr = is_server ? ip_to_uint32(client_ip) : ip_to_uint32(server_ip);
        v4_tuple.dport = SMARTNS_UDP_MAGIC_PORT;
        for (uint16_t j = SMARTNS_UDP_MAGIC_PORT + 1;j < 65535;j++) {
            v4_tuple.sport = j;
            uint32_t rss = calculate_soft_rss(v4_tuple, RSS_KEY);
            if ((rss % main_rss_size) % SMARTNS_TX_RX_CORE == i) {
                datapath_handler_list[i].txpath_handler->src_port = j;
                break;
            }
            assert(j != 65535);
        }
        for (size_t j = 0;j < SMARTNS_TX_DEPTH;j++) {
            udp_packet *packet = reinterpret_cast<udp_packet *>(reinterpret_cast<size_t>(txpath_send_buf_list[i]) + j * SMARTNS_TX_PACKET_BUFFER);

            init_udp_packet(packet, v4_tuple, is_server);
        }
    }
    SMARTNS_INFO("Datapath manager initialized\n");
}

datapath_manager::~datapath_manager() {
    ibv_destroy_flow(main_flow);
    ibv_destroy_qp(main_rss_qp);
    ibv_destroy_rwq_ind_table(main_rwq_ind_table);

    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        SmartNS::free_huge_mem(txpath_send_buf_list[i]);
        SmartNS::free_huge_mem(rxpath_recv_buf_list[i]);

        delete datapath_handler_list[i].txpath_handler;
        delete datapath_handler_list[i].rxpath_handler;
        delete datapath_handler_list[i].dma_handler;
        delete[]datapath_handler_list[i].wc_send_recv;
    }
    // free context and pd at control manager destructor
}


txpath_handler::txpath_handler(ibv_context *context, ibv_pd *pd, void *buf_addr, size_t send_buf_size):
    send_offset_handler(SMARTNS_TX_DEPTH, SMARTNS_TX_PACKET_BUFFER, 0),
    send_comp_offset_handler(SMARTNS_TX_DEPTH, SMARTNS_TX_PACKET_BUFFER, 0) {
    this->context = context;
    this->pd = pd;
    tx_depth = SMARTNS_TX_DEPTH;
    send_buf_addr = reinterpret_cast<size_t>(buf_addr);
    batch_index = 0;
    wr_index = 0;

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
    tx_qp_init_attr.cap.max_send_sge = num_sges_per_wr;
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

    for (size_t i = 0;i < num_wrs;i++) {
        for (size_t j = 0;j < num_sges_per_wr;j++) {
            send_sge_list[i * num_sges_per_wr + j].lkey = mr->lkey;
        }

        send_wr[i].sg_list = send_sge_list + i * num_sges_per_wr;
        send_wr[i].num_sge = num_sges_per_wr;
        if (i != 0) {
            send_wr[i - 1].next = send_wr + i;
        }
        send_wr[i].next = nullptr;
        send_wr[i].send_flags = IBV_SEND_IP_CSUM;
        send_wr[i].opcode = IBV_WR_SEND;
    }
}

txpath_handler::~txpath_handler() {
    free(send_sge_list);
    free(send_wr);
    free(send_bad_wr);

    ibv_destroy_qp(send_qp);
    ibv_destroy_cq(send_cq);
    ibv_dereg_mr(mr);
}

rxpath_handler::rxpath_handler(ibv_context *all_rx_context, ibv_pd *all_rx_pd, void *buf_addr, size_t recv_buf_size):
    recv_offset_handler(SMARTNS_RX_DEPTH, SMARTNS_RX_PACKET_BUFFER, 0),
    recv_comp_offset_handler(SMARTNS_RX_DEPTH, SMARTNS_RX_PACKET_BUFFER, 0) {
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
    wq_init_attr.max_sge = num_sges_per_wr;
    wq_init_attr.pd = pd;
    wq_init_attr.cq = recv_cq;
    wq_init_attr.create_flags = 0;

    assert(recv_wq = ibv_create_wq(context, &wq_init_attr));

    struct ibv_wq_attr wq_attr;
    memset(&wq_attr, 0, sizeof(struct ibv_wq_attr));
    wq_attr.wq_state = IBV_WQS_RDY;
    wq_attr.attr_mask = IBV_WQ_ATTR_STATE;
    assert(ibv_modify_wq(recv_wq, &wq_attr) == 0);

    for (size_t i = 0;i < rx_depth;i++) {
        recv_sge_list[0].addr = recv_offset_handler.offset() + recv_buf_addr;
        recv_sge_list[0].length = SMARTNS_RX_PACKET_BUFFER;
        recv_sge_list[0].lkey = mr->lkey;
        recv_wr->num_sge = 1;
        recv_wr->sg_list = recv_sge_list;
        recv_wr->wr_id = recv_offset_handler.offset() + recv_buf_addr;
        recv_wr->next = nullptr;
        assert(ibv_post_wq_recv(recv_wq, recv_wr, &recv_bad_wr) == 0);
        recv_offset_handler.step();
    }

    for (size_t i = 0;i < num_wrs;i++) {
        for (size_t j = 0;j < num_sges_per_wr;j++) {
            recv_sge_list[i * num_sges_per_wr + j].lkey = mr->lkey;
        }

        recv_wr[i].sg_list = recv_sge_list + i * num_sges_per_wr;
        recv_wr[i].num_sge = num_sges_per_wr;
        if (i != 0) {
            recv_wr[i - 1].next = recv_wr + i;
        }
        recv_wr[i].next = nullptr;
    }
}

rxpath_handler::~rxpath_handler() {
    free(recv_sge_list);
    free(recv_wr);
    free(recv_bad_wr);

    ibv_destroy_wq(recv_wq);
    ibv_destroy_cq(recv_cq);
    ibv_dereg_mr(mr);
}

dma_handler::dma_handler(ibv_context *context, ibv_pd *pd) {
    this->context = context;
    this->pd = pd;

    assert(dma_send_recv_cq = create_dma_cq(context, 128 * SMARTNS_DMA_GROUP_SIZE));
    assert(invalid_send_recv_cq = create_dma_cq(context, 128 * SMARTNS_DMA_GROUP_SIZE));

    dma_qp_list = new ibv_qp * [SMARTNS_DMA_GROUP_SIZE];
    dma_qpx_list = new ibv_qp_ex * [SMARTNS_DMA_GROUP_SIZE];
    dma_mqpx_list = new mlx5dv_qp_ex * [SMARTNS_DMA_GROUP_SIZE];
    dma_count_list = new uint64_t[SMARTNS_DMA_GROUP_SIZE];
    payload_count_list = new uint64_t[SMARTNS_DMA_GROUP_SIZE];

    invalid_qp_list = new ibv_qp * [SMARTNS_DMA_GROUP_SIZE];
    invalid_qpx_list = new ibv_qp_ex * [SMARTNS_DMA_GROUP_SIZE];
    invalid_mqpx_list = new mlx5dv_qp_ex * [SMARTNS_DMA_GROUP_SIZE];
    invalid_start_index_list = new uint64_t[SMARTNS_DMA_GROUP_SIZE];
    invalid_finish_index_list = new uint64_t[SMARTNS_DMA_GROUP_SIZE];

    now_use_qp_index = 0;
    qp_group_size = SMARTNS_DMA_GROUP_SIZE;


    for (size_t i = 0;i < SMARTNS_DMA_GROUP_SIZE;i++) {
        ibv_qp *dma_qp = create_dma_qp(context, pd, dma_send_recv_cq, dma_send_recv_cq, 128);
        init_dma_qp(dma_qp);
        dma_qp_self_connected(dma_qp);

        ibv_qp_ex *dma_qpx = ibv_qp_to_qp_ex(dma_qp);
        mlx5dv_qp_ex *dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(dma_qpx);
        // important for init before do memcpy
        dma_mqpx->wr_memcpy_direct_init(dma_mqpx);
        dma_qp_list[i] = dma_qp;
        dma_qpx_list[i] = dma_qpx;
        dma_mqpx_list[i] = dma_mqpx;
        dma_count_list[i] = 0;
        payload_count_list[i] = 0;
    }

    for (size_t i = 0;i < SMARTNS_DMA_GROUP_SIZE;i++) {
        ibv_qp *dma_qp = create_dma_qp(context, pd, dma_send_recv_cq, dma_send_recv_cq, 128);
        init_dma_qp(dma_qp);
        dma_qp_self_connected(dma_qp);

        ibv_qp_ex *dma_qpx = ibv_qp_to_qp_ex(dma_qp);
        mlx5dv_qp_ex *dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(dma_qpx);
        // important for init before do memcpy
        dma_mqpx->wr_invcache_direct_init(dma_mqpx);
        invalid_qp_list[i] = dma_qp;
        invalid_qpx_list[i] = dma_qpx;
        invalid_mqpx_list[i] = dma_mqpx;
        invalid_start_index_list[i] = 0;
        invalid_finish_index_list[i] = 0;
    }
}

dma_handler::~dma_handler() {
    for (size_t i = 0;i < SMARTNS_DMA_GROUP_SIZE;i++) {
        ibv_destroy_qp(dma_qp_list[i]);
        ibv_destroy_qp(invalid_qp_list[i]);
    }
    ibv_destroy_cq(dma_send_recv_cq);
    ibv_destroy_cq(invalid_send_recv_cq);

    delete[]dma_qp_list;
    delete[]dma_qpx_list;
    delete[]dma_mqpx_list;
    delete[]dma_count_list;
    delete[]payload_count_list;

    delete[]invalid_qp_list;
    delete[]invalid_qpx_list;
    delete[]invalid_mqpx_list;
    delete[]invalid_start_index_list;
    delete[]invalid_finish_index_list;
}

size_t datapath_handler::handle_send() {
    for (auto qp : active_qp_list) {
        rxe_handle_req(this, qp);
    }
    txpath_handler->commit_flush();
    txpath_handler->poll_tx_cq();
    return 0;
}

size_t datapath_handler::handle_recv() {
    size_t recv = rxe_handle_recv(this);
    return recv;
}

void datapath_handler::dma_send_payload_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size) {
    dpu_recv_wq *recv_wq = qp->recv_wq;
    smartns_recv_wqe *recv_wqe = qp->recv_wq->get_next_wqe();
    size_t now_size = payload_size;
    uint64_t now_buf_addr = reinterpret_cast<uint64_t>(paylod_buf);

    for (;recv_wq->now_sge_num < recv_wq->max_sge;recv_wq->now_sge_num++) {
        assert(recv_wqe[recv_wq->now_sge_num].lkey != 100);
        uint32_t remain_sge_byte = recv_wqe[recv_wq->now_sge_num].byte_count - recv_wq->now_sge_offset;
        if (remain_sge_byte >= now_size) {
            dma_handler->post_dma_req_without_cq(recv_wqe[recv_wq->now_sge_num].lkey, recv_wqe[recv_wq->now_sge_num].addr + recv_wq->now_sge_offset, rxpath_handler->mr->lkey, now_buf_addr, now_size);
            recv_wq->now_total_dma_byte += now_size;
            recv_wq->now_sge_offset += now_size;
            now_size = 0;
            if (recv_wq->now_sge_offset == recv_wqe[recv_wq->now_sge_num].byte_count) {
                recv_wq->now_sge_offset = 0;
                recv_wq->now_sge_num++;
            }
            break;
        } else {
            dma_handler->post_dma_req_without_cq(recv_wqe[recv_wq->now_sge_num].lkey, recv_wqe[recv_wq->now_sge_num].addr + recv_wq->now_sge_offset, rxpath_handler->mr->lkey, now_buf_addr, remain_sge_byte);
            recv_wq->now_total_dma_byte += remain_sge_byte;
            now_buf_addr += remain_sge_byte;
            now_size -= remain_sge_byte;
            recv_wq->now_sge_offset = 0;
        }
    }
    assert(now_size == 0);
    return;
}

void datapath_handler::dma_write_payload_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size) {
    dpu_recv_wq *recv_wq = qp->recv_wq;

    dma_handler->post_dma_req_without_cq(recv_wq->mr->devx_mr->lkey, recv_wq->host_va + recv_wq->offset,
        rxpath_handler->mr->lkey, reinterpret_cast<size_t>(paylod_buf), payload_size);

    recv_wq->offset += payload_size;
    recv_wq->resid -= payload_size;
    return;
}

void datapath_handler::dma_send_cq_to_host(dpu_qp *qp) {
    dma_handler->post_only_cq(qp->send_cq);
    // don't forget to step send_wq !!!
    // 
    qp->send_cq->step_cq();
}

void datapath_handler::dma_recv_cq_to_host(dpu_qp *qp) {
    dma_handler->post_only_cq(qp->recv_cq);
    qp->recv_wq->step_wq();
    qp->recv_cq->step_cq();
}

void datapath_handler::loop_datapath_send_wq() {
    for (auto datapath_send_wq : active_datapath_send_wq_list) {
        smartns_send_wqe *wqe;
        while ((wqe = datapath_send_wq->get_next_wqe()) != nullptr) {
            datapath_send_wq->step_wq();

            dpu_qp *qp = local_qpn_to_qp_list[wqe->qpn];
            assert(qp);

            dpu_send_wq *send_wq = qp->send_wq;

            // ADD to active list
            if (send_wq->is_empty()) {
                active_qp_list.insert(qp);
            }

            dpu_send_wqe *send_wqe = send_wq->get_next_wqe();
            send_wqe->local_addr = wqe->local_addr;
            send_wqe->local_lkey = wqe->local_lkey;
            send_wqe->byte_count = wqe->byte_count;
            send_wqe->imm = wqe->imm;
            send_wqe->remote_addr = wqe->remote_addr;
            send_wqe->remote_rkey = wqe->remote_rkey;
            send_wqe->opcode = wqe->opcode;
            send_wqe->cur_pos = wqe->cur_pos;
            send_wqe->is_signal = wqe->is_signal;

            // TODO
            send_wqe->state = dpu_send_wqe_state_posted;
            send_wqe->first_psn = 0;
            send_wqe->last_psn = 0;
            send_wqe->cur_pkt_num = 0;
            send_wqe->cur_pkt_offset = 0;
            send_wq->step_head();
        }
    }
}