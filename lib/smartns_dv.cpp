#include "smartns_dv.h"


struct ibv_context *smartns_open_device(struct ibv_device *ib_dev) {
    struct ibv_context *context = ibv_open_device(ib_dev);
    if (!context) {
        fprintf(stderr, "Error, failed to open device %s\n", ib_dev->name);
        return nullptr;
    }
    struct ibv_pd *pd = ibv_alloc_pd(context);
    if (!pd) {
        fprintf(stderr, "Error, failed to allocate PD\n");
        return nullptr;
    }
    struct smartns_context *s_ctx = new smartns_context();
    s_ctx->context = context;
    s_ctx->inner_pd = pd;

    s_ctx->host_mr_base = aligned_alloc(PAGE_SIZE, SMARTNS_CONTEXT_ALLOC_SIZE);
    assert(s_ctx->host_mr_base != nullptr);
    // create devx mr
    s_ctx->host_mr_allocator = new custom_allocator(s_ctx->host_mr_base, SMARTNS_CONTEXT_ALLOC_SIZE);
    s_ctx->inner_host_mr = devx_reg_mr(pd, s_ctx->host_mr_base, SMARTNS_CONTEXT_ALLOC_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    assert(devx_mr_allow_other_vhca_access(s_ctx->inner_host_mr, vhca_access_key, sizeof(vhca_access_key)) == 0);

    s_ctx->kernel_fd = open(smartnsinode, O_RDWR | O_CLOEXEC);
    if (s_ctx->kernel_fd < 0) {
        fprintf(stderr, "Error, failed to open %s\n", smartnsinode);
        return nullptr;
    }

    struct SMARTNS_OPEN_DEVICE_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.host_vhca_id = s_ctx->inner_host_mr->vhca_id;
    params.host_mkey = devx_mr_query_mkey(s_ctx->inner_host_mr);
    params.host_size = SMARTNS_CONTEXT_ALLOC_SIZE;
    params.host_addr = s_ctx->host_mr_base;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_OPEN_DEVICE, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_OPEN_DEVICE %d\n", retcode);
        return nullptr;
    }
    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_OPEN_DEVICE\n");
        return nullptr;
    }

    s_ctx->inner_bf_mr = devx_create_crossing_mr(pd, params.bf_addr, params.bf_size, params.bf_vhca_id, params.bf_mkey, vhca_access_key, sizeof(vhca_access_key));

    s_ctx->context_number = params.context_number;

    void *bf_base_addr = params.bf_addr;
    size_t bf_total_size = params.bf_size;

    for (uint32_t i = 0;i < params.send_wq_number;i++) {
        smartns_send_wq *send_wq = new smartns_send_wq();
        send_wq->send_wq_id = i;
        send_wq->host_mr_lkey = s_ctx->inner_host_mr->lkey;
        send_wq->bf_mr_lkey = s_ctx->inner_bf_mr->lkey;

        send_wq->host_send_wq_buf = s_ctx->host_mr_allocator->alloc(params.send_wq_capacity * sizeof(smartns_send_wqe));
        assert(send_wq->host_send_wq_buf != nullptr);

        send_wq->bf_send_wq_buf = bf_base_addr;
        assert(send_wq->bf_send_wq_buf != nullptr);
        bf_base_addr = reinterpret_cast<void *>(reinterpret_cast<size_t>(bf_base_addr) + params.send_wq_capacity * sizeof(smartns_send_wqe));
        bf_total_size -= params.send_wq_capacity * sizeof(smartns_send_wqe);

        send_wq->max_num = params.send_wq_capacity;
        send_wq->cur_num = 0;

        send_wq->dma_wq.dma_send_cq = create_dma_cq(context, 128);
        send_wq->dma_wq.dma_recv_cq = send_wq->dma_wq.dma_send_cq;
        send_wq->dma_wq.start_index = 0;
        send_wq->dma_wq.finish_index = 0;
        send_wq->dma_wq.max_num = 128;
        send_wq->dma_wq.dma_qp = create_dma_qp(context, pd, send_wq->dma_wq.dma_recv_cq, send_wq->dma_wq.dma_send_cq, 128);
        init_dma_qp(send_wq->dma_wq.dma_qp);
        dma_qp_self_connected(send_wq->dma_wq.dma_qp);

        send_wq->dma_wq.dma_qpx = ibv_qp_to_qp_ex(send_wq->dma_wq.dma_qp);
        send_wq->dma_wq.dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(send_wq->dma_wq.dma_qpx);
        send_wq->dma_wq.dma_mqpx->wr_memcpy_direct_init(send_wq->dma_wq.dma_mqpx);

        s_ctx->send_wq_list.push_back(send_wq);
    }

    s_ctx->bf_mr_allocator = new custom_allocator(bf_base_addr, bf_total_size);

    return reinterpret_cast<struct ibv_context *>(s_ctx);
}

int smartns_close_device(struct ibv_context *context) {
    struct smartns_context *s_ctx = reinterpret_cast<smartns_context *>(context);

    struct SMARTNS_CLOSE_DEVICE_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.context_number = s_ctx->context_number;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_CLOSE_DEVICE, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_CLOSE_DEVICE %d\n", retcode);
        return -1;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_CLOSE_DEVICE\n");
        return -1;
    }

    if (s_ctx->inner_bf_mr != nullptr) {
        devx_dereg_mr(s_ctx->inner_bf_mr);
    }
    if (s_ctx->inner_host_mr != nullptr) {
        devx_dereg_mr(s_ctx->inner_host_mr);
    }

    for (auto &send_wq : s_ctx->send_wq_list) {
        ibv_destroy_qp(send_wq->dma_wq.dma_qp);
        // don't need destory recv_cq because it's equal to send_cq
        ibv_destroy_cq(send_wq->dma_wq.dma_send_cq);
        delete send_wq;
    }

    delete s_ctx->host_mr_allocator;
    delete s_ctx->bf_mr_allocator;
    free(s_ctx->host_mr_base);

    close(s_ctx->kernel_fd);

    ibv_dealloc_pd(s_ctx->inner_pd);
    ibv_close_device(s_ctx->context);

    delete s_ctx;

    return 0;
}

struct ibv_pd *smartns_alloc_pd(struct ibv_context *context) {
    struct smartns_context *s_ctx = reinterpret_cast<smartns_context *>(context);

    if (s_ctx->pd_list.size() != 0) {
        return reinterpret_cast<ibv_pd *>(s_ctx->pd_list.begin()->second);
    }

    struct SMARTNS_ALLOC_PD_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.context_number = s_ctx->context_number;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_ALLOC_PD, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_ALLOC_PD %d\n", retcode);
        return nullptr;
    }
    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_ALLOC_PD\n");
        return nullptr;
    }

    struct smartns_pd *s_pd = new smartns_pd();
    s_pd->pd = nullptr;
    s_pd->pd_number = params.pd_number;
    s_pd->context = s_ctx;

    s_ctx->pd_list[params.pd_number] = s_pd;

    return reinterpret_cast<struct ibv_pd *>(s_pd);
}

int smartns_dealloc_pd(struct ibv_pd *pd) {
    struct smartns_pd *s_pd = reinterpret_cast<smartns_pd *>(pd);
    struct smartns_context *s_ctx = s_pd->context;

    struct SMARTNS_DEALLOC_PD_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.context_number = s_ctx->context_number;
    params.pd_number = s_pd->pd_number;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_DEALLOC_PD, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_DEALLOC_PD %d\n", retcode);
        return -1;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_DEALLOC_PD\n");
        return -1;
    }

    s_ctx->pd_list.erase(s_pd->pd_number);

    delete s_pd;

    return 0;
}

struct ibv_mr *smartns_reg_mr(struct ibv_pd *pd, void *addr, size_t length, unsigned int access) {
    struct smartns_pd *s_pd = reinterpret_cast<smartns_pd *>(pd);
    struct smartns_context *s_ctx = s_pd->context;

    struct smartns_mr *s_mr = new smartns_mr();
    s_mr->dev_mr = devx_reg_mr(s_ctx->inner_pd, addr, length, access);
    assert(devx_mr_allow_other_vhca_access(s_mr->dev_mr, vhca_access_key, sizeof(vhca_access_key)) == 0);

    struct SMARTNS_REG_MR_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.pd_number = s_pd->pd_number;
    params.host_vhca_id = s_mr->dev_mr->vhca_id;
    params.host_mkey = devx_mr_query_mkey(s_mr->dev_mr);
    params.host_size = length;
    params.host_addr = addr;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_REG_MR, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_REG_MR %d\n", retcode);
        return nullptr;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_REG_MR\n");
        return nullptr;
    }

    s_mr->mr.context = reinterpret_cast<ibv_context *>(s_ctx);
    s_mr->mr.pd = reinterpret_cast<ibv_pd *>(s_pd);
    s_mr->mr.addr = addr;
    s_mr->mr.length = length;
    s_mr->mr.handle = s_mr->dev_mr->handle;
    s_mr->mr.lkey = params.host_mkey;
    s_mr->mr.rkey = params.host_mkey;

    return reinterpret_cast<struct ibv_mr *>(s_mr);
}

int smartns_dereg_mr(struct ibv_mr *mr) {
    struct smartns_mr *s_mr = reinterpret_cast<smartns_mr *>(mr);
    struct smartns_context *s_ctx = reinterpret_cast<struct smartns_context *>(s_mr->mr.context);

    struct SMARTNS_DESTROY_MR_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.pd_number = reinterpret_cast<struct smartns_pd *>(s_mr->mr.pd)->pd_number;
    params.host_mkey = s_mr->mr.lkey;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_DESTROY_MR, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_DESTROY_MR %d\n", retcode);
        return -1;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_DESTROY_MR\n");
        return -1;
    }

    devx_dereg_mr(s_mr->dev_mr);

    delete s_mr;

    return 0;
}

struct ibv_cq *smartns_create_cq(struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector) {
    struct smartns_context *s_ctx = reinterpret_cast<smartns_context *>(context);

    void *host_cq_buf = s_ctx->host_mr_allocator->alloc(cqe * sizeof(struct smartns_cqe), PAGE_SIZE);
    void *host_cq_doorbell = s_ctx->host_mr_allocator->alloc(sizeof(struct smartns_cq_doorbell), sizeof(struct smartns_cq_doorbell));
    void *bf_cq_buf = s_ctx->bf_mr_allocator->alloc(cqe * sizeof(struct smartns_cqe), PAGE_SIZE);
    void *bf_cq_doorbell = s_ctx->bf_mr_allocator->alloc(sizeof(struct smartns_cq_doorbell), sizeof(struct smartns_cq_doorbell));
    assert(host_cq_buf != nullptr);
    assert(host_cq_doorbell != nullptr);
    assert(bf_cq_buf != nullptr);
    assert(bf_cq_doorbell != nullptr);

    struct SMARTNS_CREATE_CQ_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.max_num = cqe;
    params.host_cq_buf = host_cq_buf;
    params.host_cq_doorbell = host_cq_doorbell;
    params.bf_cq_buf = bf_cq_buf;
    params.bf_cq_doorbell = bf_cq_doorbell;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_CREATE_CQ, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_CREATE_CQ %d\n", retcode);
        return nullptr;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_CREATE_CQ\n");
        return nullptr;
    }

    struct smartns_cq *s_cq = new smartns_cq();
    s_cq->cq = nullptr;
    s_cq->context = s_ctx;
    s_cq->cq_number = params.cq_number;
    s_cq->host_cq_buf = host_cq_buf;
    s_cq->host_cq_doorbell = reinterpret_cast<smartns_cq_doorbell *>(host_cq_doorbell);
    s_cq->bf_cq_buf = bf_cq_buf;
    s_cq->bf_cq_doorbell = reinterpret_cast<smartns_cq_doorbell *>(bf_cq_doorbell);
    s_cq->max_num = cqe;
    s_cq->cur_num = 0;

    s_ctx->cq_list[params.cq_number] = s_cq;

    return reinterpret_cast<struct ibv_cq *>(s_cq);
}

int smartns_destroy_cq(struct ibv_cq *cq) {
    struct smartns_cq *s_cq = reinterpret_cast<smartns_cq *>(cq);
    struct smartns_context *s_ctx = s_cq->context;

    struct SMARTNS_DESTROY_CQ_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.cq_number = s_cq->cq_number;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_DESTROY_CQ, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_DESTROY_CQ %d\n", retcode);
        return -1;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_DESTROY_CQ\n");
        return -1;
    }

    s_ctx->cq_list.erase(params.cq_number);

    s_ctx->host_mr_allocator->free(s_cq->host_cq_buf);
    s_ctx->host_mr_allocator->free(s_cq->host_cq_doorbell);
    s_ctx->bf_mr_allocator->free(s_cq->bf_cq_buf);
    s_ctx->bf_mr_allocator->free(s_cq->bf_cq_doorbell);

    delete s_cq;

    return 0;
}

ibv_qp *smartns_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    struct smartns_pd *s_pd = reinterpret_cast<smartns_pd *>(pd);
    struct smartns_context *s_ctx = s_pd->context;

    assert(s_ctx->pd_list.count(s_pd->pd_number) != 0);

    struct smartns_cq *send_cq = reinterpret_cast<smartns_cq *>(qp_init_attr->send_cq);
    struct smartns_cq *recv_cq = reinterpret_cast<smartns_cq *>(qp_init_attr->recv_cq);

    assert(s_ctx->cq_list.count(send_cq->cq_number) != 0);
    assert(s_ctx->cq_list.count(recv_cq->cq_number) != 0);

    // use sq_sig_all as send_wq_id
    assert(static_cast<size_t>(qp_init_attr->sq_sig_all) < s_ctx->send_wq_list.size());

    uint32_t		max_send_wr = qp_init_attr->cap.max_send_wr;
    uint32_t		max_recv_wr = qp_init_attr->cap.max_recv_wr;
    uint32_t		max_send_sge = qp_init_attr->cap.max_send_sge;
    uint32_t		max_recv_sge = qp_init_attr->cap.max_recv_sge;
    uint32_t		max_inline_data = qp_init_attr->cap.max_inline_data;
    enum ibv_qp_type	qp_type = qp_init_attr->qp_type;

    size_t recv_wq_size = max_recv_sge * max_recv_wr * sizeof(struct smartns_recv_wqe);
    void *host_recv_wq_addr = s_ctx->host_mr_allocator->alloc(recv_wq_size, PAGE_SIZE);
    void *bf_recv_wq_addr = s_ctx->bf_mr_allocator->alloc(recv_wq_size, PAGE_SIZE);

    struct SMARTNS_CREATE_QP_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.pd_number = s_pd->pd_number;
    params.send_wq_id = qp_init_attr->sq_sig_all;
    params.recv_wq_size = recv_wq_size;
    params.host_recv_wq_addr = host_recv_wq_addr;
    params.bf_recv_wq_addr = bf_recv_wq_addr;
    params.send_cq_number = send_cq->cq_number;
    params.recv_cq_number = recv_cq->cq_number;
    params.max_send_wr = max_send_wr;
    params.max_recv_wr = max_recv_wr;
    params.max_send_sge = max_send_sge;
    params.max_recv_sge = max_recv_sge;
    params.max_inline_data = max_inline_data;
    params.qp_type = qp_type;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_CREATE_QP, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_CREATE_QP %d\n", retcode);
        return nullptr;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_CREATE_QP\n");
        return nullptr;
    }

    struct smartns_qp *s_qp = new smartns_qp();
    s_qp->qp = nullptr;
    s_qp->context = s_ctx;
    s_qp->pd = s_pd;
    s_qp->qp_number = params.qp_number;
    s_qp->max_send_wr = max_send_wr;
    s_qp->max_recv_wr = max_recv_wr;
    s_qp->max_send_sge = max_send_sge;
    s_qp->max_recv_sge = max_recv_sge;
    s_qp->max_inline_data = max_inline_data;
    s_qp->qp_type = qp_type;
    s_qp->cur_qp_state = IBV_QPS_RESET;

    s_qp->send_cq = send_cq;
    s_qp->recv_cq = recv_cq;

    s_qp->send_wq = s_ctx->send_wq_list[qp_init_attr->sq_sig_all];
    s_qp->recv_wq = new smartns_recv_wq();

    s_qp->recv_wq->host_mr_lkey = s_ctx->inner_host_mr->lkey;
    s_qp->recv_wq->bf_mr_lkey = s_ctx->inner_bf_mr->lkey;
    s_qp->recv_wq->host_recv_wq_buf = host_recv_wq_addr;
    s_qp->recv_wq->bf_recv_wq_buf = bf_recv_wq_addr;
    s_qp->recv_wq->max_num = max_recv_wr;
    s_qp->recv_wq->max_sge = max_recv_sge;
    s_qp->recv_wq->cur_num = 0;

    s_qp->recv_wq->dma_wq.dma_send_cq = create_dma_cq(s_ctx->context, 128);
    s_qp->recv_wq->dma_wq.dma_recv_cq = s_qp->recv_wq->dma_wq.dma_send_cq;
    s_qp->recv_wq->dma_wq.start_index = 0;
    s_qp->recv_wq->dma_wq.finish_index = 0;
    s_qp->recv_wq->dma_wq.max_num = 128;

    s_qp->recv_wq->dma_wq.dma_qp = create_dma_qp(s_ctx->context, s_ctx->inner_pd, s_qp->recv_wq->dma_wq.dma_recv_cq, s_qp->recv_wq->dma_wq.dma_send_cq, 128);
    init_dma_qp(s_qp->recv_wq->dma_wq.dma_qp);
    dma_qp_self_connected(s_qp->recv_wq->dma_wq.dma_qp);

    s_qp->recv_wq->dma_wq.dma_qpx = ibv_qp_to_qp_ex(s_qp->recv_wq->dma_wq.dma_qp);
    s_qp->recv_wq->dma_wq.dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(s_qp->recv_wq->dma_wq.dma_qpx);
    s_qp->recv_wq->dma_wq.dma_mqpx->wr_memcpy_direct_init(s_qp->recv_wq->dma_wq.dma_mqpx);

    s_ctx->qp_list[params.qp_number] = s_qp;

    return reinterpret_cast<ibv_qp *>(s_qp);
}

// TODO 
int smartns_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask) {
    struct smartns_qp *s_qp = reinterpret_cast<smartns_qp *>(qp);
    return 0;
}

int smartns_destroy_qp(struct ibv_qp *qp) {
    struct smartns_qp *s_qp = reinterpret_cast<smartns_qp *>(qp);
    struct smartns_context *s_ctx = s_qp->context;
    struct SMARTNS_DESTROY_QP_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.pd_number = reinterpret_cast<smartns_pd *>(s_qp->pd)->pd_number;
    params.qp_number = s_qp->qp_number;

    int retcode = ioctl(s_ctx->kernel_fd, SMARTNS_IOC_DESTROY_QP, &params);
    if (retcode < 0) {
        fprintf(stderr, "Error, failed to ioctl SMARTNS_IOC_DESTROY_QP %d\n", retcode);
        return -1;
    }

    if (params.common_params.success == 0) {
        fprintf(stderr, "Error, failed to exec ioctl SMARTNS_IOC_DESTROY_QP\n");
        return -1;
    }

    s_ctx->qp_list.erase(params.qp_number);

    s_ctx->host_mr_allocator->free(s_qp->recv_wq->host_recv_wq_buf);
    s_ctx->bf_mr_allocator->free(s_qp->recv_wq->bf_recv_wq_buf);

    ibv_destroy_qp(s_qp->recv_wq->dma_wq.dma_qp);
    ibv_destroy_cq(s_qp->recv_wq->dma_wq.dma_send_cq);
    delete s_qp->recv_wq;
    delete s_qp;

    return 0;
}

