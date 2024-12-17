#include "smartns.h"
#include "numautil.h"
#include "rdma_cm/libr.h"

controlpath_manager::controlpath_manager(std::string device_name, size_t numa_node, bool is_server) {
    this->numa_node = numa_node;
    this->is_server = is_server;
    this->device_name = device_name;

    control_rdma_param.device_name = device_name;
    control_rdma_param.numa_node = numa_node;

    roce_init(control_rdma_param, 1);

    send_recv_buf_size = (control_tx_depth + control_rx_depth) * control_packet_size;
    send_recv_buf = SmartNS::get_huge_mem(numa_node, send_recv_buf_size);
    for (size_t j = 0;j < send_recv_buf_size / sizeof(size_t);j++) {
        ((size_t *)send_recv_buf)[j] = 0;
    }

    control_qp_handler = create_qp_rc(control_rdma_param, send_recv_buf, send_recv_buf_size, &local_bf_info, 0);

    assert(control_qp_handler);

    for (size_t i = 0;i < 6;i++) {
        local_bf_info.mac[i] = is_server ? server_mac[i] : client_mac[i];
    }

    send_handler.init(control_tx_depth, control_packet_size, 0);
    send_comp_handler.init(control_tx_depth, control_packet_size, 0);
    recv_handler.init(control_rx_depth, control_packet_size, control_tx_depth * control_packet_size);
    recv_comp_handler.init(control_rx_depth, control_packet_size, control_tx_depth * control_packet_size);

    control_net_param.isServer = true;
    control_net_param.sock_port = SMARTNS_TCP_PORT;

    global_context = control_rdma_param.contexts[0];
    global_pd = control_qp_handler->pd;
}

controlpath_manager::~controlpath_manager() {
    free(control_qp_handler->send_sge_list);
    free(control_qp_handler->recv_sge_list);
    free(control_qp_handler->send_wr);
    free(control_qp_handler->recv_wr);

    ibv_destroy_qp(control_qp_handler->qp);
    ibv_dereg_mr(control_qp_handler->mr);
    ibv_destroy_cq(control_qp_handler->send_cq);
    ibv_destroy_cq(control_qp_handler->recv_cq);
    ibv_dealloc_pd(control_qp_handler->pd);

    ibv_close_device(control_rdma_param.contexts[0]);

    SmartNS::free_huge_mem(send_recv_buf);
    free(control_qp_handler);
}

void controlpath_manager::handle_open_device(SMARTNS_OPEN_DEVICE_PARAMS *param) {
    dpu_context *dpu_ctx = new dpu_context();
    dpu_ctx->context_number = generate_context_number();
    dpu_ctx->host_pid = param->common_params.pid;
    dpu_ctx->host_tgid = param->common_params.tgid;

    dpu_ctx->inner_host_mr = devx_create_crossing_mr(global_pd, param->host_addr, param->host_size,
        param->host_vhca_id, param->host_mkey, vhca_access_key, sizeof(vhca_access_key));
    assert(dpu_ctx->inner_host_mr);

    void *bf_mr_base = aligned_alloc(PAGE_SIZE, SMARTNS_CONTEXT_ALLOC_SIZE);
    assert(bf_mr_base);
    memset(bf_mr_base, 0, SMARTNS_CONTEXT_ALLOC_SIZE);

    dpu_ctx->inner_bf_mr = devx_reg_mr(global_pd, bf_mr_base, SMARTNS_CONTEXT_ALLOC_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    assert(devx_mr_allow_other_vhca_access(dpu_ctx->inner_bf_mr, vhca_access_key, sizeof(vhca_access_key)) == 0);

    for (size_t i = 0;i < SMARTNS_TX_RX_CORE;i++) {
        dpu_send_wq send_wq;
        send_wq.dpu_ctx = dpu_ctx;
        send_wq.bf_send_wq_buf = bf_mr_base;
        send_wq.max_num = SMARTNS_TX_DEPTH;
        send_wq.cur_num = 0;
        send_wq.send_wq_id = i;

        dpu_ctx->send_wq_list.emplace_back(send_wq);

        bf_mr_base = reinterpret_cast<void *>(reinterpret_cast<size_t>(bf_mr_base) + sizeof(smartns_send_wqe) * SMARTNS_TX_DEPTH);
    }

    // TODO add send_wq to each datapath
    context_list[dpu_ctx->context_number] = dpu_ctx;

    param->bf_vhca_id = dpu_ctx->inner_bf_mr->vhca_id;
    param->bf_mkey = devx_mr_query_mkey(dpu_ctx->inner_bf_mr);
    param->bf_size = SMARTNS_CONTEXT_ALLOC_SIZE;
    param->bf_addr = dpu_ctx->inner_bf_mr->addr;

    param->context_number = dpu_ctx->context_number;
    param->send_wq_number = SMARTNS_TX_RX_CORE;
    param->send_wq_capacity = SMARTNS_TX_DEPTH;

    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_close_device(SMARTNS_CLOSE_DEVICE_PARAMS *param) {
    auto it = context_list.count(param->context_number);
    if (it == 0) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }

    dpu_context *dpu_ctx = context_list[0];
    if (dpu_ctx->cq_list.size() != 0) {
        SMARTNS_ERROR("context number %lu has %lu cq not destroyed", param->context_number, dpu_ctx->cq_list.size());
        exit(1);
    }
    if (dpu_ctx->pd_list.size() != 0) {
        SMARTNS_ERROR("context number %lu has %lu pd not destroyed", param->context_number, dpu_ctx->pd_list.size());
        exit(1);
    }
    if (dpu_ctx->qp_list.size() != 0) {
        SMARTNS_ERROR("context number %lu has %lu qp not destroyed", param->context_number, dpu_ctx->qp_list.size());
        exit(1);
    }
    if (dpu_ctx->mr_list.size() != 0) {
        SMARTNS_ERROR("context number %lu has %lu mr not destroyed", param->context_number, dpu_ctx->mr_list.size());
        exit(1);
    }

    // TODO del each datapath send_wq

    free(dpu_ctx->inner_bf_mr->addr);

    devx_dereg_mr(dpu_ctx->inner_bf_mr);
    devx_dereg_mr(dpu_ctx->inner_host_mr);

    free(dpu_ctx);

    context_list.erase(it);

    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_alloc_pd(SMARTNS_ALLOC_PD_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }

    if (dpu_ctx->host_tgid != param->common_params.tgid) {
        SMARTNS_ERROR("context number %lu host_tgid %d not match %d", param->context_number, dpu_ctx->host_tgid, param->common_params.tgid);
        exit(1);
    }

    if (dpu_ctx->pd_list.size()) {
        SMARTNS_ERROR("context number %lu already has pd", param->context_number);
        exit(1);
    }

    dpu_pd *pd = new dpu_pd();
    pd->dpu_ctx = dpu_ctx;
    pd->pd_number = generate_pd_number();

    dpu_ctx->pd_list[pd->pd_number] = pd;

    param->pd_number = pd->pd_number;
    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_dealloc_pd(SMARTNS_DEALLOC_PD_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }
    dpu_pd *pd = dpu_ctx->pd_list[param->pd_number];
    if (!pd) {
        SMARTNS_ERROR("context number %lu pd number %lu not found", param->context_number, param->pd_number);
        exit(1);
    }

    dpu_ctx->pd_list.erase(param->pd_number);
    delete pd;

    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_reg_mr(SMARTNS_REG_MR_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }
    dpu_pd *pd = dpu_ctx->pd_list[param->pd_number];
    if (!pd) {
        SMARTNS_ERROR("context number %lu pd number %lu not found", param->context_number, param->pd_number);
        exit(1);
    }

    dpu_mr *mr = new dpu_mr();
    mr->host_mkey = param->host_mkey;

    mr->devx_mr = devx_create_crossing_mr(global_pd, param->host_addr, param->host_size, param->host_vhca_id, param->host_mkey, vhca_access_key, sizeof(vhca_access_key));
    assert(mr->devx_mr);

    dpu_ctx->mr_list[mr->host_mkey] = mr;

    param->bf_mkey = mr->devx_mr->lkey;
    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_destory_mr(SMARTNS_DESTROY_MR_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }
    dpu_pd *pd = dpu_ctx->pd_list[param->pd_number];
    if (!pd) {
        SMARTNS_ERROR("context number %lu pd number %lu not found", param->context_number, param->pd_number);
        exit(1);
    }
    dpu_mr *mr = dpu_ctx->mr_list[param->host_mkey];
    if (!mr) {
        SMARTNS_ERROR("context number %lu mr host mkey %u not found", param->context_number, param->host_mkey);
        exit(1);
    }

    dpu_ctx->mr_list.erase(param->host_mkey);
    assert(devx_dereg_mr(mr->devx_mr) == 0);
    delete mr;

    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_create_cq(SMARTNS_CREATE_CQ_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }
    dpu_cq *cq = new dpu_cq();
    cq->dpu_ctx = dpu_ctx;
    cq->cq_number = generate_cq_number();
    cq->max_num = param->max_num;
    cq->cur_num = 0;
    cq->host_cq_buf = param->host_cq_buf;
    cq->host_cq_doorbell = param->host_cq_doorbell;
    cq->bf_cq_buf = param->bf_cq_buf;
    cq->bf_cq_doorbell = param->bf_cq_doorbell;

    dpu_ctx->cq_list[cq->cq_number] = cq;

    param->cq_number = cq->cq_number;
    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_destory_cq(SMARTNS_DESTROY_CQ_PARAMS *param) {
    dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }
    dpu_cq *cq = dpu_ctx->cq_list[param->cq_number];
    if (!cq) {
        SMARTNS_ERROR("context number %lu cq number %lu not found", param->context_number, param->cq_number);
        exit(1);
    }

    dpu_ctx->cq_list.erase(param->cq_number);

    delete cq;

    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_create_qp(SMARTNS_CREATE_QP_PARAMS *param) {
    struct dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }

    struct dpu_pd *pd = dpu_ctx->pd_list[param->pd_number];
    if (!pd) {
        SMARTNS_ERROR("context number %lu pd number %lu not found", param->context_number, param->pd_number);
        exit(1);
    }

    struct dpu_cq *send_cq = dpu_ctx->cq_list[param->send_cq_number];
    if (!send_cq) {
        SMARTNS_ERROR("context number %lu send cq number %lu not found", param->context_number, param->send_cq_number);
        exit(1);
    }

    struct dpu_cq *recv_cq = dpu_ctx->cq_list[param->recv_cq_number];
    if (!recv_cq) {
        SMARTNS_ERROR("context number %lu recv cq number %lu not found", param->context_number, param->recv_cq_number);
        exit(1);
    }

    assert(param->send_wq_id < SMARTNS_TX_RX_CORE);
    struct dpu_send_wq *send_wq = &dpu_ctx->send_wq_list[param->send_wq_id];
    struct dpu_recv_wq *recv_wq = new dpu_recv_wq();
    recv_wq->dpu_ctx = dpu_ctx;
    recv_wq->bf_recv_wq_buf = param->bf_recv_wq_addr;

    recv_wq->wqe_size = max_(1, param->max_recv_sge) * sizeof(smartns_recv_wqe);
    recv_wq->wqe_cnt = param->max_recv_wr;
    recv_wq->wqe_shift = std::log2(recv_wq->wqe_size);
    recv_wq->max_sge = param->max_recv_sge;
    recv_wq->head = 0;
    recv_wq->own_flag = 1;

    dpu_qp *qp = new dpu_qp();
    qp->dpu_ctx = dpu_ctx;
    qp->dpu_pd = pd;
    qp->qp_number = generate_qp_number();
    qp->qp_type = static_cast<ibv_qp_type>(param->qp_type);
    qp->max_send_wr = param->max_send_wr;
    qp->max_recv_wr = param->max_recv_wr;
    qp->max_send_sge = param->max_send_sge;
    qp->max_recv_sge = param->max_recv_sge;
    qp->max_inline_data = param->max_inline_data;

    qp->send_cq = send_cq;
    qp->recv_cq = recv_cq;
    qp->send_wq = send_wq;
    qp->recv_wq = recv_wq;

    dpu_ctx->qp_list[qp->qp_number] = qp;

    // add to special datapath
    data_manager->datapath_handler_list[param->send_wq_id].local_qpn_to_qp_list[qp->qp_number] = qp;

    param->qp_number = qp->qp_number;
    param->common_params.success = 1;
    return;
}

void controlpath_manager::handle_destory_qp(SMARTNS_DESTROY_QP_PARAMS *param) {
    struct dpu_context *dpu_ctx = context_list[param->context_number];
    if (!dpu_ctx) {
        SMARTNS_ERROR("context number %lu not found", param->context_number);
        exit(1);
    }

    struct dpu_pd *pd = dpu_ctx->pd_list[param->pd_number];
    if (!pd) {
        SMARTNS_ERROR("context number %lu pd number %lu not found", param->context_number, param->pd_number);
        exit(1);
    }

    struct dpu_qp *qp = dpu_ctx->qp_list[param->qp_number];
    if (!qp) {
        SMARTNS_ERROR("context number %lu qp number %lu not found", param->context_number, param->qp_number);
        exit(1);
    }

    // remove from special datapath
    data_manager->datapath_handler_list[qp->send_wq->send_wq_id].local_qpn_to_qp_list.erase(param->qp_number);

    dpu_ctx->qp_list.erase(param->qp_number);
    delete qp;

    param->common_params.success = 1;
    return;
}


void controlpath_manager::handle_modify_qp(SMARTNS_MODIFY_QP_PARAMS *param) {

}

size_t controlpath_manager::generate_context_number() {
    static size_t context_number = 0;
    return context_number++;
}

size_t controlpath_manager::generate_pd_number() {
    static size_t pd_number = 0;
    return pd_number++;
}

size_t controlpath_manager::generate_cq_number() {
    static size_t cq_number = 0;
    return cq_number++;
}

size_t controlpath_manager::generate_qp_number() {
    static size_t qp_number = 0;
    return qp_number++;
}
