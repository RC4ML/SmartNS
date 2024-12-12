#include "smartns_kernel.h"

int smartns_create_qp_and_send_to_bf(struct smartns_qp_handler *now_info) {
    int ret = 0;
    int index = 0;
    unsigned long page_start;
    struct page *now_page;
    struct ib_cq_init_attr send_cq_attr = {
        .cqe = SMARTNS_TX_DEPTH,
        .comp_vector = 0,
        .flags = 0
    };
    struct ib_cq_init_attr recv_cq_attr = {
        .cqe = SMARTNS_RX_DEPTH,
        .comp_vector = 0,
        .flags = 0
    };
    struct ib_qp_init_attr qp_init_attr;
    struct ib_qp_attr qp_attr;
    int flags = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS;
    union ib_gid temp_gid;
    struct ib_port_attr port_attr;

    struct PingPongInfo pingpong_info;

    now_info->pd = ib_alloc_pd(global_device, 0);
    if (!now_info->pd) {
        pr_err("%s: failed to allocate pd\n", MODULE_NAME);
        return -ENOMEM;
    }
    now_info->mr = ib_alloc_mr(now_info->pd, IB_MR_TYPE_MEM_REG, SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE);
    if (!now_info->mr) {
        pr_err("%s: failed to allocate mr\n", MODULE_NAME);
        return -ENOMEM;
    }
    now_info->original_buf = (size_t)vmalloc(SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE);
    if (!now_info->original_buf) {
        pr_err("%s: failed to allocate original_buf\n", MODULE_NAME);
        return -ENOMEM;
    }
    now_info->sg = kcalloc(SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE, sizeof(struct scatterlist), GFP_KERNEL);
    if (!now_info->sg) {
        pr_err("%s: failed to allocate sg\n", MODULE_NAME);
        return -ENOMEM;
    }
    sg_init_table(now_info->sg, SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE);
    for (index = 0;index < SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE;index++) {
        page_start = (unsigned long)now_info->original_buf + (index * PAGE_SIZE);
        now_page = vmalloc_to_page((void *)page_start);
        if (!now_page) {
            pr_err("%s: failed to get page\n", MODULE_NAME);
            return -ENOMEM;
        }
        sg_set_page(now_info->sg + index, now_page, PAGE_SIZE, 0);
    }

    now_info->sg_offset = 0;
    if (ib_dma_map_sg(global_device, now_info->sg, SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE, DMA_BIDIRECTIONAL) < 0) {
        pr_err("%s: failed to map sg\n", MODULE_NAME);
        return -ENOMEM;
    }

    index = ib_map_mr_sg(now_info->mr, now_info->sg, SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE, &now_info->sg_offset, PAGE_SIZE);
    if (index < 0 || index != SMARTNS_TX_DEPTH * SMARTNS_MSG_SIZE / PAGE_SIZE) {
        pr_err("%s: failed to map mr sg\n", MODULE_NAME);
        return -ENOMEM;
    }

    now_info->local_buf = now_info->original_buf;
    now_info->local_dma_buf = now_info->sg->dma_address;

    now_info->send_cq = ib_create_cq(global_device, NULL, NULL, NULL, &send_cq_attr);
    now_info->recv_cq = ib_create_cq(global_device, NULL, NULL, NULL, &recv_cq_attr);

    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = now_info->send_cq;
    qp_init_attr.recv_cq = now_info->recv_cq;
    qp_init_attr.cap.max_inline_data = 0;
    qp_init_attr.cap.max_send_wr = SMARTNS_TX_DEPTH;
    qp_init_attr.cap.max_send_sge = SMARTNS_NUM_SGES_PER_WR;
    qp_init_attr.cap.max_recv_wr = SMARTNS_RX_DEPTH;
    qp_init_attr.cap.max_recv_sge = SMARTNS_NUM_SGES_PER_WR;
    qp_init_attr.qp_type = IB_QPT_RC;
    now_info->qp = ib_create_qp(now_info->pd, &qp_init_attr);
    if (!now_info->qp) {
        pr_err("%s: failed to create qp\n", MODULE_NAME);
        return -ENOMEM;
    }

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IB_QPS_INIT;
    qp_attr.pkey_index = 0;
    qp_attr.port_num = 1;
    qp_attr.qp_access_flags = IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE;
    if ((ret = ib_modify_qp(now_info->qp, &qp_attr, flags))) {
        pr_err("%s: failed to modify qp to init errno: %d\n", MODULE_NAME, ret);
        return -ENOMEM;
    }

    ib_query_port(global_device, 1, &port_attr);
    rdma_query_gid(global_device, 1, SMARTNS_RDMA_GID_INDEX, &temp_gid);
    pingpong_info.lid = port_attr.lid;
    pingpong_info.gid_index = SMARTNS_RDMA_GID_INDEX;
    pingpong_info.qpn = now_info->qp->qp_num;
    pingpong_info.psn = now_info->qp->qp_num & 0xffffff;
    pingpong_info.rkey = now_info->mr->rkey;
    pingpong_info.out_reads = 1;
    pingpong_info.vaddr = now_info->local_dma_buf;
    memcpy(pingpong_info.raw_gid, temp_gid.raw, 16);

    now_info->num_wrs = SMARTNS_NUM_WRS;
    now_info->send_sge_list = kmalloc(sizeof(struct ib_sge) * SMARTNS_NUM_SGES_PER_WR * now_info->num_wrs, GFP_KERNEL);
    now_info->recv_sge_list = kmalloc(sizeof(struct ib_sge) * SMARTNS_NUM_SGES_PER_WR * now_info->num_wrs, GFP_KERNEL);
    now_info->send_wr = kmalloc(sizeof(struct ib_send_wr) * now_info->num_wrs, GFP_KERNEL);
    now_info->recv_wr = kmalloc(sizeof(struct ib_recv_wr) * now_info->num_wrs, GFP_KERNEL);
    now_info->send_wc = kmalloc(sizeof(struct ib_wc) * SMARTNS_CQ_POLL_BATCH, GFP_KERNEL);
    now_info->recv_wc = kmalloc(sizeof(struct ib_wc) * SMARTNS_CQ_POLL_BATCH, GFP_KERNEL);
    now_info->num_sges = SMARTNS_NUM_SGES_PER_WR * now_info->num_wrs;
    now_info->num_sges_per_wr = SMARTNS_NUM_SGES_PER_WR;
    now_info->tx_depth = SMARTNS_TX_DEPTH;
    now_info->rx_depth = SMARTNS_RX_DEPTH;

    pr_info("%s: Create success, local QPN:%#06x\n", MODULE_NAME, now_info->qp->qp_num);

    return tcp_client_send(global_tcp_socket, (const char *)&pingpong_info, sizeof(struct PingPongInfo), MSG_DONTWAIT);
}

void smartns_send_reg_mr(struct smartns_qp_handler *info) {
    struct ib_reg_wr wr;
    wr.wr.next = NULL;

    wr.wr.opcode = IB_WR_REG_MR;
    wr.wr.num_sge = 0;
    wr.wr.send_flags = IB_SEND_SIGNALED;
    wr.mr = info->mr;
    wr.key = info->mr->lkey;
    wr.access = IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_READ | IB_ACCESS_REMOTE_WRITE;
    ib_post_send(info->qp, &wr.wr, NULL);

    while (ib_poll_cq(info->send_cq, 1, info->send_wc) == 0) {
    }
    if (info->send_wc->status != IB_WC_SUCCESS) {
        pr_err("%s: failed to register mr\n", MODULE_NAME);
    } else {
        pr_info("%s: mr registered\n", MODULE_NAME);
    }
}
void smartns_init_wr_base_send_recv(struct smartns_qp_handler *info) {
    struct ib_send_wr *send_wr = info->send_wr;
    struct ib_recv_wr *recv_wr = info->recv_wr;
    struct ib_sge *send_sge_list = info->send_sge_list, *recv_sge_list = info->recv_sge_list;
    int index = 0;
    memset(send_wr, 0, sizeof(struct ib_send_wr) * info->num_wrs);
    memset(recv_wr, 0, sizeof(struct ib_recv_wr) * info->num_wrs);

    for (;index < info->num_wrs;index++) {
        send_sge_list[index].addr = info->local_dma_buf;
        send_sge_list[index].lkey = info->mr->lkey;

        send_wr[index].sg_list = send_sge_list + index * info->num_sges_per_wr;
        send_wr[index].num_sge = info->num_sges_per_wr;
        send_wr[index].wr_id = 1000;
        send_wr[index].next = NULL;
        send_wr[index].send_flags = IB_SEND_SIGNALED;
        send_wr[index].opcode = IB_WR_SEND;
        if (index > 0) {
            send_wr[index - 1].next = send_wr + index;
        }

        recv_sge_list[index].addr = info->local_dma_buf;
        recv_sge_list[index].lkey = info->mr->lkey;
        recv_wr[index].sg_list = recv_sge_list + index * info->num_sges_per_wr;
        recv_wr[index].num_sge = info->num_sges_per_wr;
        recv_wr[index].wr_id = 1001;
        recv_wr[index].next = NULL;
        if (index > 0) {
            recv_wr[index - 1].next = recv_wr + index;
        }
    }
}

int smartns_init_qp(struct smartns_qp_handler *now_info, struct PingPongInfo *pingpong_info) {
    int ret = 0;
    struct ib_qp_attr attr;
    int flags = 0;

    flags = IB_QP_STATE;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IB_QPS_RTR;
    attr.ah_attr.port_num = 1;
    attr.ah_attr.ah_flags = IB_AH_GRH; // important
    attr.ah_attr.sl = 0;//service level default 0
    memcpy(attr.ah_attr.grh.dgid.raw, pingpong_info->raw_gid, 16);
    attr.ah_attr.grh.sgid_index = SMARTNS_RDMA_GID_INDEX;
    attr.ah_attr.grh.hop_limit = 0xFF;
    attr.ah_attr.grh.traffic_class = 0;
    // TODO maybe need add rdma_ah_attr_type?
    attr.ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
    memcpy(attr.ah_attr.roce.dmac, pingpong_info->mac, 6);

    attr.path_mtu = ilog2(current_mtu / 128);
    attr.dest_qp_num = pingpong_info->qpn;
    attr.rq_psn = pingpong_info->psn;
    flags |= (IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN | IB_QP_RQ_PSN);

    attr.max_dest_rd_atomic = pingpong_info->out_reads;
    attr.min_rnr_timer = SMARTNS_MIN_RNR_TIMER;
    flags |= (IB_QP_MIN_RNR_TIMER | IB_QP_MAX_DEST_RD_ATOMIC);

    if ((ret = ib_modify_qp(now_info->qp, &attr, flags))) {
        pr_err("%s: failed to modify qp to RTR errno: %d\n", MODULE_NAME, ret);
        return -ENOMEM;
    }

    flags = IB_QP_STATE;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IB_QPS_RTS;
    flags |= IB_QP_SQ_PSN;
    attr.sq_psn = now_info->qp->qp_num & 0xffffff; // just a hack!

    attr.timeout = SMARTNS_DEF_QP_TIME;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = pingpong_info->out_reads;
    flags |= (IB_QP_TIMEOUT | IB_QP_RETRY_CNT | IB_QP_RNR_RETRY | IB_QP_MAX_QP_RD_ATOMIC);
    if (ib_modify_qp(now_info->qp, &attr, flags)) {
        pr_err("%s: failed to modify qp to RTS errno: %d\n", MODULE_NAME, ret);
        return -ENOMEM;
    }

    smartns_init_wr_base_send_recv(now_info);

    smartns_send_reg_mr(now_info);

    pr_info("%s: Connected success, local QPN:%#06x, remote QPN:%#08x\n", MODULE_NAME, now_info->qp->qp_num, pingpong_info->qpn);

    return ret;
}

int smartns_free_qp(struct smartns_qp_handler *now_info) {
    if (!now_info) {
        pr_err("%s: failed to get qp info\n", MODULE_NAME);
        return -ENOMEM;
    }
    kfree(now_info->send_sge_list);
    kfree(now_info->recv_sge_list);
    kfree(now_info->send_wr);
    kfree(now_info->recv_wr);
    kfree(now_info->send_wc);
    kfree(now_info->recv_wc);
    kfree(now_info->sg);

    ib_destroy_qp(now_info->qp);
    ib_dereg_mr(now_info->mr);
    ib_destroy_cq(now_info->send_cq);
    ib_destroy_cq(now_info->recv_cq);
    ib_dealloc_pd(now_info->pd);

    vfree((void *)now_info->original_buf);

    pr_info("%s: Free QP success, local QPN:%#06x\n", MODULE_NAME, now_info->qp->qp_num);

    return 0;
}