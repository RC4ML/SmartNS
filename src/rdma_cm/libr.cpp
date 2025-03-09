#include "rdma_cm/libr.h"

const char *transport_type_str(enum ibv_transport_type t) {
    switch (t) {
    case IBV_TRANSPORT_UNKNOWN: return "IBV_TRANSPORT_UNKNOWN";
    case IBV_TRANSPORT_IB: return "IBV_TRANSPORT_IB";
    case IBV_TRANSPORT_IWARP: return "IBV_TRANSPORT_IWARP";
    case IBV_TRANSPORT_USNIC: return "IBV_TRANSPORT_USNIC";
    case IBV_TRANSPORT_USNIC_UDP: return "IBV_TRANSPORT_USNIC_UDP";
    case IBV_TRANSPORT_UNSPECIFIED: return "IBV_TRANSPORT_UNSPECIFIED";
    }
    return "Unknow TransportType";
}

const char *link_layer_str(int8_t link_layer) {
    switch (link_layer) {
    case IBV_LINK_LAYER_UNSPECIFIED:
    case IBV_LINK_LAYER_INFINIBAND:
        return "IB";
    case IBV_LINK_LAYER_ETHERNET:
        return "Ethernet";
    default:
        SMARTNS_ERROR("Unkonwn link layer");
        return "Unknown";
    }
}

struct ibv_device *ctx_find_dev(char const *ib_devname) {
    int num_of_device;
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev = NULL;

    dev_list = ibv_get_device_list(&num_of_device);

    assert(num_of_device > 0);

    if (!ib_devname) {
        ib_dev = dev_list[0];
        assert(ib_dev != NULL);
    } else {
        for (; (ib_dev = *dev_list); ++dev_list)
            if (!strcmp(ibv_get_device_name(ib_dev), ib_devname))
                break;
        assert(ib_dev != NULL);
    }
    SMARTNS_INFO("%-20s : %s", "IB_DEV_NAME", ibv_get_device_name(ib_dev));
    return ib_dev;
}

struct ibv_context *ctx_open_device(struct ibv_device *ib_dev) {
    struct ibv_context *context;
    context = ibv_open_device(ib_dev);
    assert(context != NULL);
    return context;
}

struct ibv_context *ctx_open_devx_device(struct ibv_device *ib_dev) {
    struct mlx5dv_context_attr dv_attr = {};

    dv_attr.flags |= MLX5DV_CONTEXT_FLAGS_DEVX;
    struct ibv_context *context = mlx5dv_open_device(ib_dev, &dv_attr);

    if (context == NULL) {
        printf("failed to create context\n");
        exit(__LINE__);
    }
    return context;
}

/**
 * @brief Init rdma context
 *
 * @param[in, out] rdma_param will be filled with contexts
 * @param[in] num_contexts number of contexts
 */
void roce_init(rdma_param &rdma_param, int num_contexts) {
    rdma_param.num_contexts = num_contexts;
    ALLOCATE(rdma_param.contexts, ibv_context *, num_contexts);
    struct ibv_device *ib_dev = ctx_find_dev(rdma_param.device_name.c_str());
    for (int i = 0;i < num_contexts;i++) {
        if (rdma_param.use_devx_context) {
            rdma_param.contexts[i] = ctx_open_devx_device(ib_dev);
            SMARTNS_INFO("Use devx context %d\n", i);
        } else {
            rdma_param.contexts[i] = ctx_open_device(ib_dev);
        }
    }
    struct ibv_context *context = rdma_param.contexts[0];

    //check link
    SMARTNS_INFO("%-20s : %s", "Transport type", transport_type_str(context->device->transport_type));
    struct ibv_port_attr port_attr;
    assert(ibv_query_port(context, rdma_param.ib_port, &port_attr) == 0);
    SMARTNS_INFO("%-20s : %s", "Line Layer", link_layer_str(port_attr.link_layer));
    assert(port_attr.state == IBV_PORT_ACTIVE);
    struct ibv_device_attr attr;
    assert(ibv_query_device(context, &attr) == 0);
    SMARTNS_INFO("%-20s : %d", "Max Outreads", attr.max_qp_rd_atom);
    SMARTNS_INFO("%-20s : %d", "Max Pkeys", attr.max_pkeys);
    SMARTNS_INFO("%-20s : %d", "Atomic Capacity", attr.atomic_cap);

    //set mtu
    assert((port_attr.active_mtu >= IBV_MTU_256 && port_attr.active_mtu <= IBV_MTU_4096));
    rdma_param.cur_mtu = port_attr.active_mtu;
    rdma_param.max_out_read = attr.max_qp_rd_atom;
    SMARTNS_INFO("%-20s : %d", "CUR MTU", 128 << (rdma_param.cur_mtu));
}

/**
 * @brief Create a qp rc object
 *
 * @param[in, out] rdma_param
 * @param[in] buf
 * @param[in] size
 * @param[out] info to be exchanged with remote
 * @param[in] context_index thread index
 * @return qp_handler*
 */
qp_handler *create_qp_rc(rdma_param &rdma_param, void *buf, size_t size, struct pingpong_info *info, int context_index) {
    assert(context_index < rdma_param.num_contexts);
    qp_handler *qp_handler;
    ALLOCATE(qp_handler, class qp_handler, 1);

    int num_wrs = rdma_param.batch_size != 0 ? rdma_param.batch_size : 1;
    int num_sges_per_wr = rdma_param.sge_per_wr != 0 ? rdma_param.sge_per_wr : 1;
    int num_sges = num_wrs * num_sges_per_wr;
    struct ibv_sge *send_sge_list;
    struct ibv_sge *recv_sge_list;
    struct ibv_send_wr *send_wr;
    struct ibv_send_wr *send_bar_wr;
    struct ibv_recv_wr *recv_wr;
    struct ibv_recv_wr *recv_bar_wr;

    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_comp_channel *channel = NULL;
    struct ibv_qp *qp;

    int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;//send/recv/read/write 

    ALLOCATE(send_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(recv_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(send_wr, struct ibv_send_wr, num_wrs);
    ALLOCATE(send_bar_wr, struct ibv_send_wr, 1);
    ALLOCATE(recv_wr, struct ibv_recv_wr, num_wrs);
    ALLOCATE(recv_bar_wr, struct ibv_recv_wr, 1);

    //check valid mem
    assert(size > static_cast<size_t>(PAGE_SIZE));
    assert((reinterpret_cast<size_t>(buf)) % PAGE_SIZE == 0);

    //create pd/mr/scq/rcq
    assert(pd = ibv_alloc_pd(rdma_param.contexts[context_index]));
    assert(mr = ibv_reg_mr(pd, buf, size, flags));
    assert(send_cq = ibv_create_cq(rdma_param.contexts[context_index], rdma_param.tx_depth, NULL, channel, 0));
    assert(recv_cq = ibv_create_cq(rdma_param.contexts[context_index], rdma_param.rx_depth, NULL, channel, 0));

    //create qp
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.cap.max_inline_data = rdma_param.max_inline_size;
    attr.cap.max_send_wr = rdma_param.tx_depth;
    attr.cap.max_send_sge = num_sges_per_wr;
    attr.cap.max_recv_wr = rdma_param.rx_depth;
    attr.cap.max_recv_sge = num_sges_per_wr;
    attr.qp_type = IBV_QPT_RC;
    qp = ibv_create_qp(pd, &attr);
    if (qp == NULL && errno == ENOMEM) {
        SMARTNS_ERROR("Requested QP size might be too big. Try reducing TX depth and/or inline size.\n");
        SMARTNS_ERROR("Current TX depth is %d and inline size is %d .\n", rdma_param.tx_depth, rdma_param.max_inline_size);
    }
    assert(rdma_param.max_inline_size <= attr.cap.max_inline_data);

    //modify qp to init
    struct ibv_qp_attr attr_qp;
    memset(&attr_qp, 0, sizeof(struct ibv_qp_attr));
    flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;
    flags |= IBV_QP_ACCESS_FLAGS;
    attr_qp.qp_state = IBV_QPS_INIT;
    attr_qp.pkey_index = 0;
    attr_qp.port_num = rdma_param.ib_port;
    attr_qp.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;//for send
    assert(ibv_modify_qp(qp, &attr_qp, flags) == 0);

    //setup connection
    union ibv_gid temp_gid;
    struct ibv_port_attr attr_port;
    assert(ibv_query_port(rdma_param.contexts[context_index], rdma_param.ib_port, &attr_port) == 0);
    assert(ibv_query_gid(rdma_param.contexts[context_index], rdma_param.ib_port, rdma_param.gid_index, &temp_gid) == 0);
    info->lid = attr_port.lid;//local id, it seems only useful for ib instead of roce
    info->gid_index = rdma_param.gid_index;
    info->qpn = qp->qp_num;
    info->psn = lrand48() & 0xffffff;
    info->rkey = mr->rkey;
    info->out_reads = rdma_param.max_out_read;
    info->vaddr = reinterpret_cast<uintptr_t>(buf);
    info->mtu = rdma_param.cur_mtu;
    memcpy(info->raw_gid, temp_gid.raw, 16);

    qp_handler->buf = reinterpret_cast<size_t> (buf);
    qp_handler->send_cq = send_cq;
    qp_handler->recv_cq = recv_cq;
    qp_handler->max_inline_size = rdma_param.max_inline_size;
    qp_handler->qp = qp;
    qp_handler->pd = pd;
    qp_handler->mr = mr;
    qp_handler->send_sge_list = send_sge_list;
    qp_handler->recv_sge_list = recv_sge_list;
    qp_handler->send_wr = send_wr;
    qp_handler->send_bar_wr = send_bar_wr;
    qp_handler->recv_wr = recv_wr;
    qp_handler->recv_bar_wr = recv_bar_wr;
    qp_handler->num_sges = num_sges;
    qp_handler->num_sges_per_wr = num_sges_per_wr;
    qp_handler->num_wrs = num_wrs;
    qp_handler->tx_depth = rdma_param.tx_depth;
    qp_handler->rx_depth = rdma_param.rx_depth;

    return qp_handler;
}

/**
 * @brief Establish Reliable Connection between two QPs
 *
 * QP state will be changed to RTR and RTS
 *
 * @param[in] rdma_param
 * @param[in] qp_handler
 * @param[in] remote_info
 * @param[in] local_info
 */
void connect_qp_rc(rdma_param &rdma_param, qp_handler &qp_handler, struct pingpong_info *remote_info, struct pingpong_info *local_info) {
    struct ibv_ah *ah;//todo
    (void)ah;
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof attr);
    int flags = IBV_QP_STATE;
    attr.qp_state = IBV_QPS_RTR;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = rdma_param.ib_port;
    attr.ah_attr.dlid = remote_info->lid;
    attr.ah_attr.sl = 0;//service level default 0
    attr.ah_attr.is_global = 1;
    memcpy(attr.ah_attr.grh.dgid.raw, remote_info->raw_gid, 16);
    attr.ah_attr.grh.sgid_index = rdma_param.gid_index;
    attr.ah_attr.grh.hop_limit = 0xFF;
    attr.ah_attr.grh.traffic_class = 0;

    //UD does not need below code
    attr.path_mtu = static_cast<enum ibv_mtu>(std::min(remote_info->mtu, local_info->mtu));
    attr.dest_qp_num = remote_info->qpn;
    attr.rq_psn = remote_info->psn;
    flags |= (IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN);

    //only for RC
    attr.max_dest_rd_atomic = remote_info->out_reads;
    attr.min_rnr_timer = MIN_RNR_TIMER;
    flags |= (IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC);

    //modify qp to rtr
    assert(ibv_modify_qp(qp_handler.qp, &attr, flags) == 0);
    SMARTNS_INFO("Connected success, local QPN:%#06x, remote QPN:%#08x", local_info->qpn, remote_info->qpn);

    {
        //modify qp to rts
        flags = IBV_QP_STATE;
        struct ibv_qp_attr *_attr = &attr;
        _attr->qp_state = IBV_QPS_RTS;
        flags |= IBV_QP_SQ_PSN;
        _attr->sq_psn = local_info->psn;

        //only for RC
        _attr->timeout = DEF_QP_TIME;
        _attr->retry_cnt = 7;
        _attr->rnr_retry = 7;
        _attr->max_rd_atomic = remote_info->out_reads;
        flags |= (IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
        assert(ibv_modify_qp(qp_handler.qp, _attr, flags) == 0);
    }
    ah = ibv_create_ah(qp_handler.pd, &(attr.ah_attr));

    qp_handler.remote_buf = remote_info->vaddr;
    qp_handler.remote_rkey = remote_info->rkey;

    // TODO
    // init wr
}

qp_handler *create_qp_raw_packet(rdma_param &rdma_param, void *buf, size_t size, uint32_t tx_depth, uint32_t rx_depth, int context_index) {
    assert(context_index < rdma_param.num_contexts);
    qp_handler *handler;
    ALLOCATE(handler, qp_handler, 1);
    uint32_t max_inline_size = 0;

    int num_wrs = rdma_param.batch_size != 0 ? rdma_param.batch_size : 1;
    int num_sges_per_wr = rdma_param.sge_per_wr != 0 ? rdma_param.sge_per_wr : 1;
    int num_sges = num_wrs * num_sges_per_wr;
    struct ibv_sge *send_sge_list;
    struct ibv_sge *recv_sge_list;
    struct ibv_send_wr *send_wr;
    struct ibv_send_wr *send_bar_wr;
    struct ibv_recv_wr *recv_wr;
    struct ibv_recv_wr *recv_bar_wr;

    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_comp_channel *channel = NULL;
    struct ibv_qp *qp;

    int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;//send/recv/read/write 

    ALLOCATE(send_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(recv_sge_list, struct ibv_sge, num_sges);
    ALLOCATE(send_wr, struct ibv_send_wr, num_wrs);
    ALLOCATE(send_bar_wr, struct ibv_send_wr, 1);
    ALLOCATE(recv_wr, struct ibv_recv_wr, num_wrs);
    ALLOCATE(recv_bar_wr, struct ibv_recv_wr, 1);

    //check valid mem
    assert(size > static_cast<size_t>(PAGE_SIZE));
    assert((reinterpret_cast<size_t>(buf)) % PAGE_SIZE == 0);

    //create pd/mr/scq/rcq
    assert(pd = ibv_alloc_pd(rdma_param.contexts[context_index]));
    assert(mr = ibv_reg_mr(pd, buf, size, flags));
    assert(send_cq = ibv_create_cq(rdma_param.contexts[context_index], tx_depth, NULL, channel, 0));
    assert(recv_cq = ibv_create_cq(rdma_param.contexts[context_index], rx_depth, NULL, channel, 0));

    //create qp
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.cap.max_inline_data = max_inline_size;
    attr.cap.max_send_wr = tx_depth;
    attr.cap.max_send_sge = num_sges_per_wr;
    attr.cap.max_recv_wr = rx_depth;
    attr.cap.max_recv_sge = num_sges_per_wr;
    attr.qp_type = IBV_QPT_RAW_PACKET;
    qp = ibv_create_qp(pd, &attr);
    if (qp == NULL && errno == ENOMEM) {
        fprintf(stderr, "Requested QP size might be too big. Try reducing TX depth and/or inline size.\n");
        fprintf(stderr, "Current TX depth is %d and inline size is %d .\n", tx_depth, max_inline_size);
    }
    assert(max_inline_size <= attr.cap.max_inline_data);

    //modify qp to init
    struct ibv_qp_attr tx_qp_attr;
    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_INIT;
    tx_qp_attr.port_num = 1;
    assert(ibv_modify_qp(qp, &tx_qp_attr, IBV_QP_STATE | IBV_QP_PORT) == 0);

    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_RTR;
    assert(ibv_modify_qp(qp, &tx_qp_attr, IBV_QP_STATE) == 0);

    memset(&tx_qp_attr, 0, sizeof(tx_qp_attr));
    tx_qp_attr.qp_state = IBV_QPS_RTS;
    assert(ibv_modify_qp(qp, &tx_qp_attr, IBV_QP_STATE) == 0);


    handler->buf = reinterpret_cast<size_t> (buf);
    handler->send_cq = send_cq;
    handler->recv_cq = recv_cq;
    handler->max_inline_size = max_inline_size;
    handler->qp = qp;
    handler->pd = pd;
    handler->mr = mr;
    handler->send_sge_list = send_sge_list;
    handler->recv_sge_list = recv_sge_list;
    handler->send_wr = send_wr;
    handler->send_bar_wr = send_bar_wr;
    handler->recv_wr = recv_wr;
    handler->recv_bar_wr = recv_bar_wr;
    handler->num_sges = num_sges;
    handler->num_sges_per_wr = num_sges_per_wr;
    handler->num_wrs = num_wrs;
    handler->tx_depth = tx_depth;
    handler->rx_depth = rx_depth;

    return handler;
}

/**
 * @brief Init send/recv Work Request
 *
 * Will init send_wr, recv_wr, send_sge_list, recv_sge_list
 *
 * @param[in, out] qp_handler
 */
void init_wr_base_send_recv(qp_handler &qp_handler) {
    //send
    assert(qp_handler.num_wrs * qp_handler.num_sges_per_wr == qp_handler.num_sges);
    memset(qp_handler.send_wr, 0, sizeof(struct ibv_send_wr) * qp_handler.num_wrs);
    ibv_send_wr *send_wr = qp_handler.send_wr;
    ibv_sge *send_sge_list = qp_handler.send_sge_list;

    for (int i = 0;i < qp_handler.num_wrs;i++) {
        send_sge_list[i].addr = qp_handler.buf;
        send_sge_list[i].lkey = qp_handler.mr->lkey;
        send_wr[i].sg_list = send_sge_list + i * qp_handler.num_sges_per_wr;
        send_wr[i].num_sge = qp_handler.num_sges_per_wr;
        send_wr[i].wr_id = 1000;//todo
        send_wr[i].next = NULL;
        send_wr[i].send_flags = IBV_SEND_SIGNALED;
        send_wr[i].opcode = IBV_WR_SEND;
        if (i > 0) {
            send_wr[i - 1].next = &send_wr[i];
        }
    }

    //recv
    memset(qp_handler.recv_wr, 0, sizeof(struct ibv_recv_wr) * qp_handler.num_wrs);
    ibv_recv_wr *recv_wr = qp_handler.recv_wr;
    ibv_sge *recv_sge_list = qp_handler.recv_sge_list;

    for (int i = 0;i < qp_handler.num_wrs;i++) {
        recv_sge_list[i].addr = qp_handler.buf;
        recv_sge_list[i].lkey = qp_handler.mr->lkey;
        recv_wr[i].sg_list = recv_sge_list + i * qp_handler.num_sges_per_wr;
        recv_wr[i].num_sge = qp_handler.num_sges_per_wr;
        recv_wr[i].wr_id = 1001;//todo
        recv_wr[i].next = NULL;//todo
        if (i > 0) {
            recv_wr[i - 1].next = &recv_wr[i];
        }
    }

}

/**
 * @brief Init write Work Request
 *
 * Difference with read is opcode
 *
 * Will init send_wr, send_sge_list
 *
 * @param[in, out] qp_handler
 * @see init_wr_base_read
 */
void init_wr_base_write(qp_handler &qp_handler) {
    //write
    assert(qp_handler.num_wrs * qp_handler.num_sges_per_wr == qp_handler.num_sges);
    memset(qp_handler.send_wr, 0, sizeof(struct ibv_send_wr) * qp_handler.num_wrs);
    ibv_send_wr *send_wr = qp_handler.send_wr;
    ibv_sge *send_sge_list = qp_handler.send_sge_list;

    for (int i = 0;i < qp_handler.num_wrs;i++) {
        send_sge_list[i].addr = qp_handler.buf;
        send_sge_list[i].lkey = qp_handler.mr->lkey;
        send_wr[i].wr.rdma.remote_addr = qp_handler.remote_buf;
        send_wr[i].wr.rdma.rkey = qp_handler.remote_rkey;

        send_wr[i].sg_list = send_sge_list + i * qp_handler.num_sges_per_wr;
        send_wr[i].num_sge = qp_handler.num_sges_per_wr;
        send_wr[i].wr_id = 0;//todo
        send_wr[i].next = NULL;
        send_wr[i].send_flags = IBV_SEND_SIGNALED;
        send_wr[i].opcode = IBV_WR_RDMA_WRITE;
    }
}

/**
 * @brief Init read Work Request
 *
 * Difference with write is opcode
 *
 * Will init send_wr, send_sge_list
 *
 * @param[in, out] qp_handler
 * @see init_wr_base_write
 */
void init_wr_base_read(qp_handler &qp_handler) {
    //read
    assert(qp_handler.num_wrs * qp_handler.num_sges_per_wr == qp_handler.num_sges);
    memset(qp_handler.send_wr, 0, sizeof(struct ibv_send_wr) * qp_handler.num_wrs);
    ibv_send_wr *send_wr = qp_handler.send_wr;
    ibv_sge *send_sge_list = qp_handler.send_sge_list;

    for (int i = 0;i < qp_handler.num_wrs;i++) {
        send_sge_list[i].addr = qp_handler.buf;
        send_sge_list[i].lkey = qp_handler.mr->lkey;
        send_wr[i].wr.rdma.remote_addr = qp_handler.remote_buf;
        send_wr[i].wr.rdma.rkey = qp_handler.remote_rkey;

        send_wr[i].sg_list = send_sge_list + i * qp_handler.num_sges_per_wr;
        send_wr[i].num_sge = qp_handler.num_sges_per_wr;
        send_wr[i].wr_id = 0;//todo
        send_wr[i].next = NULL;
        send_wr[i].send_flags = IBV_SEND_SIGNALED;
        send_wr[i].opcode = IBV_WR_RDMA_READ;
    }
}

void print_pingpong_info(struct pingpong_info *info) {
    uint16_t dlid = info->lid;
    SMARTNS_INFO(INFO_FMT, dlid, info->qpn, info->psn,
        info->rkey, info->vaddr,
        "GID",
        info->raw_gid[0], info->raw_gid[1],
        info->raw_gid[2], info->raw_gid[3],
        info->raw_gid[4], info->raw_gid[5],
        info->raw_gid[6], info->raw_gid[7],
        info->raw_gid[8], info->raw_gid[9],
        info->raw_gid[10], info->raw_gid[11],
        info->raw_gid[12], info->raw_gid[13],
        info->raw_gid[14], info->raw_gid[15]);
}

/**
 * @brief Post Send Work Request
 *
 * use ibv_post_send to post send wr
 *
 * @param[in, out] qp_handler
 * @param[in] offset
 * @param[in] length
 */
void post_send(qp_handler &qp_handler, size_t offset, int length) {
    qp_handler.send_sge_list[0].addr = qp_handler.buf + offset;
    qp_handler.send_sge_list[0].length = length;
    if (length <= qp_handler.max_inline_size) {
        qp_handler.send_wr[0].send_flags |= IBV_SEND_INLINE;
    }
    qp_handler.send_wr->wr_id = offset;
    qp_handler.send_wr->next = NULL;

    assert(ibv_post_send(qp_handler.qp, qp_handler.send_wr, &qp_handler.send_bar_wr) == 0);
}

/**
 * @brief Post Send Work Request in batch
 *
 * use ibv_post_send to post send wr
 *
 * send_wr will be chained to be send in batch
 *
 * @param[in, out] qp_handler
 * @param[in] batch_size
 * @param[in] handler
 * @param[in] length
 */
void post_send_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length) {
    assert(batch_size <= qp_handler.num_wrs);
    for (int i = 0;i < batch_size;i++) {
        qp_handler.send_sge_list[i].addr = qp_handler.buf + handler.offset();
        qp_handler.send_sge_list[i].length = length;
        if (length <= qp_handler.max_inline_size) {
            qp_handler.send_wr[i].send_flags |= IBV_SEND_INLINE;
        }
        qp_handler.send_wr[i].wr_id = handler.offset();
        qp_handler.send_wr[i].next = NULL;
        // if (handler.index() % SEND_CQ_BATCH == SEND_CQ_BATCH - 1) {
        //     qp_handler.send_wr[i].send_flags = IBV_SEND_SIGNALED;
        // } else {
        //     qp_handler.send_wr[i].send_flags = 0;
        // }

        qp_handler.send_wr[i].send_flags = IBV_SEND_SIGNALED;

        if (i > 0) {
            qp_handler.send_wr[i - 1].next = &qp_handler.send_wr[i];
        }

        handler.step();
    }
    assert(ibv_post_send(qp_handler.qp, qp_handler.send_wr, &qp_handler.send_bar_wr) == 0);
}

void post_recv(qp_handler &qp_handler, size_t offset, int length) {
    qp_handler.recv_sge_list[0].addr = qp_handler.buf + offset;
    qp_handler.recv_sge_list[0].length = length;
    qp_handler.recv_wr->wr_id = offset;
    qp_handler.recv_wr->next = NULL;
    assert(ibv_post_recv(qp_handler.qp, qp_handler.recv_wr, &qp_handler.recv_bar_wr) == 0);
}

void post_recv_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length) {
    assert(batch_size <= qp_handler.num_wrs);
    for (int i = 0;i < batch_size;i++) {
        qp_handler.recv_sge_list[i].addr = qp_handler.buf + handler.offset();
        qp_handler.recv_sge_list[i].length = length;
        qp_handler.recv_wr[i].wr_id = handler.offset();
        qp_handler.recv_wr[i].next = NULL;
        if (i > 0) {
            qp_handler.recv_wr[i - 1].next = &qp_handler.recv_wr[i];
        }
        handler.step();
    }
    assert(ibv_post_recv(qp_handler.qp, qp_handler.recv_wr, &qp_handler.recv_bar_wr) == 0);
}

/**
 * @brief
 *
 * @param[in] qp_handler
 * @param[in] wc
 * @return int
 */
int poll_send_cq(qp_handler &qp_handler, struct ibv_wc *wc) {
    int ne = ibv_poll_cq(qp_handler.send_cq, CTX_POLL_BATCH, wc);//if error, ne < 0
    return ne;
}

int poll_recv_cq(qp_handler &qp_handler, struct ibv_wc *wc) {
    int ne = ibv_poll_cq(qp_handler.recv_cq, CTX_POLL_BATCH, wc);
    return ne;
}
