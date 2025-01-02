#include "rdma_cm/libsmartns.h"
#include "smartns_dv.h"

void smartns_roce_init(rdma_param &rdma_param, int num_contexts) {
    rdma_param.num_contexts = num_contexts;
    ALLOCATE(rdma_param.contexts, ibv_context *, num_contexts);

    struct ibv_device *ib_dev = ctx_find_dev(rdma_param.device_name.c_str());

    for (int i = 0;i < num_contexts;i++) {
        rdma_param.contexts[i] = smartns_open_device(ib_dev);
        assert(rdma_param.contexts[i]);
    }
}

qp_handler *smartns_create_qp_rc(rdma_param &rdma_param, void *buf, size_t size, struct pingpong_info *info, int context_index, int qp_index) {
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
    assert(pd = smartns_alloc_pd(rdma_param.contexts[context_index]));
    assert(mr = smartns_reg_mr(pd, buf, size, flags));
    assert(send_cq = smartns_create_cq(rdma_param.contexts[context_index], rdma_param.tx_depth, NULL, channel, 0));
    assert(recv_cq = smartns_create_cq(rdma_param.contexts[context_index], rdma_param.rx_depth, NULL, channel, 0));

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
    // important
    attr.sq_sig_all = qp_index;
    attr.qp_type = IBV_QPT_RC;
    qp = smartns_create_qp(pd, &attr);
    if (qp == NULL && errno == ENOMEM) {
        SMARTNS_ERROR("Requested QP size might be too big. Try reducing TX depth and/or inline size.\n");
        SMARTNS_ERROR("Current TX depth is %d and inline size is %d .\n", rdma_param.tx_depth, rdma_param.max_inline_size);
    }
    assert(rdma_param.max_inline_size <= attr.cap.max_inline_data);

    struct smartns_qp *s_qp = reinterpret_cast<smartns_qp *>(qp);

    info->qpn = s_qp->qp_number;
    info->psn = lrand48() & 0xffffff;
    info->rkey = mr->rkey;
    info->vaddr = reinterpret_cast<uintptr_t>(buf);

    qp_handler->buf = reinterpret_cast<size_t> (buf);
    qp_handler->send_cq = send_cq;
    qp_handler->recv_cq = recv_cq;
    qp_handler->max_inline_size = rdma_param.max_out_read;
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

void smartns_connect_qp_rc(rdma_param &rdma_param, qp_handler &qp_handler, struct pingpong_info *remote_info, struct pingpong_info *local_info) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof attr);

    attr.dest_qp_num = remote_info->qpn;
    int flags = IBV_QP_STATE;
    flags |= IBV_QP_DEST_QPN;
    attr.qp_state = IBV_QPS_RTS;

    assert(smartns_modify_qp(qp_handler.qp, &attr, flags) == 0);

    qp_handler.remote_buf = remote_info->vaddr;
    qp_handler.remote_rkey = remote_info->rkey;
}


void smartns_post_send(qp_handler &qp_handler, size_t offset, int length) {
    qp_handler.send_sge_list[0].addr = qp_handler.buf + offset;
    qp_handler.send_sge_list[0].length = length;
    qp_handler.send_wr[0].send_flags = IBV_SEND_SIGNALED;
    qp_handler.send_wr->wr_id = offset;
    qp_handler.send_wr->wr.rdma.remote_addr = qp_handler.remote_buf + offset;
    qp_handler.send_wr->next = NULL;

    assert(smartns_post_send(qp_handler.qp, qp_handler.send_wr, &qp_handler.send_bar_wr) == 0);
}

void smartns_post_send_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length) {
    assert(batch_size <= qp_handler.num_wrs);
    for (int i = 0;i < batch_size;i++) {
        qp_handler.send_sge_list[i].addr = qp_handler.buf + handler.offset();
        qp_handler.send_sge_list[i].length = length;
        if (length <= qp_handler.max_inline_size) {
            qp_handler.send_wr[i].send_flags |= IBV_SEND_INLINE;
        }
        qp_handler.send_wr[i].wr_id = handler.offset();
        qp_handler.send_wr->wr.rdma.remote_addr = qp_handler.remote_buf + handler.offset();
        qp_handler.send_wr[i].next = NULL;
        if (handler.index() % SEND_CQ_BATCH == SEND_CQ_BATCH - 1) {
            qp_handler.send_wr[i].send_flags = IBV_SEND_SIGNALED;
        } else {
            qp_handler.send_wr[i].send_flags = 0;
        }

        if (i > 0) {
            qp_handler.send_wr[i - 1].next = &qp_handler.send_wr[i];
        }

        handler.step();
    }
    assert(smartns_post_send(qp_handler.qp, qp_handler.send_wr, &qp_handler.send_bar_wr) == 0);
}

void smartns_post_recv(qp_handler &qp_handler, size_t offset, int length) {
    qp_handler.recv_sge_list[0].addr = qp_handler.buf + offset;
    qp_handler.recv_sge_list[0].length = length;
    qp_handler.recv_wr->wr_id = offset;
    qp_handler.recv_wr->next = NULL;
    assert(smartns_post_recv(qp_handler.qp, qp_handler.recv_wr, &qp_handler.recv_bar_wr) == 0);
}

void smartns_post_recv_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length) {
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
    assert(smartns_post_recv(qp_handler.qp, qp_handler.recv_wr, &qp_handler.recv_bar_wr) == 0);
}

int smartns_poll_send_cq(qp_handler &qp_handler, struct ibv_wc *wc) {
    int ne = smartns_poll_cq(qp_handler.send_cq, CTX_POLL_BATCH, wc);//if error, ne < 0
    return ne;
}

int smartns_poll_recv_cq(qp_handler &qp_handler, struct ibv_wc *wc) {
    int ne = smartns_poll_cq(qp_handler.recv_cq, CTX_POLL_BATCH, wc);
    return ne;
}