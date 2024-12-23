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
    memset(s_ctx->host_mr_base, 0, SMARTNS_CONTEXT_ALLOC_SIZE);
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
        send_wq->datapath_send_wq_id = i;
        send_wq->host_mr_lkey = s_ctx->inner_host_mr->lkey;
        send_wq->bf_mr_lkey = s_ctx->inner_bf_mr->lkey;

        send_wq->host_send_wq_buf = s_ctx->host_mr_allocator->alloc(params.send_wq_capacity * sizeof(smartns_send_wqe));
        assert(send_wq->host_send_wq_buf != nullptr);

        send_wq->bf_send_wq_buf = bf_base_addr;
        assert(send_wq->bf_send_wq_buf != nullptr);
        bf_base_addr = reinterpret_cast<void *>(reinterpret_cast<size_t>(bf_base_addr) + params.send_wq_capacity * sizeof(smartns_send_wqe));
        bf_total_size -= params.send_wq_capacity * sizeof(smartns_send_wqe);

        send_wq->wqe_size = sizeof(smartns_send_wqe);
        send_wq->wqe_cnt = params.send_wq_capacity;
        send_wq->wqe_shift = std::log2(send_wq->wqe_size);
        send_wq->max_sge = 1;
        send_wq->wrid = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t) * send_wq->wqe_cnt));

        send_wq->head = 0;
        send_wq->tail = 0;

        send_wq->own_flag = 1;

        send_wq->dma_wq.dma_send_recv_cq = create_dma_cq(context, 256);
        send_wq->dma_wq.start_index = 0;
        send_wq->dma_wq.dma_index = 0;
        send_wq->dma_wq.finish_index = 0;
        send_wq->dma_wq.max_num = 256;
        send_wq->dma_wq.dma_batch_size = 1;
        send_wq->dma_wq.host_addr = send_wq->host_send_wq_buf;
        send_wq->dma_wq.bf_addr = send_wq->bf_send_wq_buf;
        send_wq->dma_wq.wqe_size = sizeof(smartns_send_wqe);
        send_wq->dma_wq.wqe_cnt = params.send_wq_capacity;

        send_wq->dma_wq.dma_qp = create_dma_qp(context, pd, send_wq->dma_wq.dma_send_recv_cq, send_wq->dma_wq.dma_send_recv_cq, 256);
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
        free(send_wq->wrid);
        ibv_destroy_qp(send_wq->dma_wq.dma_qp);
        ibv_destroy_cq(send_wq->dma_wq.dma_send_recv_cq);
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
    s_mr->bf_mkey = params.bf_mkey;

    s_ctx->mr_list[s_mr->mr.lkey] = s_mr;

    return reinterpret_cast<struct ibv_mr *>(s_mr);
}

int smartns_dereg_mr(struct ibv_mr *mr) {
    struct smartns_mr *s_mr = reinterpret_cast<smartns_mr *>(mr);
    struct smartns_context *s_ctx = reinterpret_cast<struct smartns_context *>(s_mr->mr.context);

    if (s_ctx->mr_list.count(s_mr->mr.lkey) == 0) {
        fprintf(stderr, "Error, mr %u not found\n", s_mr->mr.lkey);
        return -1;
    }

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

    s_ctx->mr_list.erase(s_mr->mr.lkey);

    delete s_mr;

    return 0;
}

struct ibv_cq *smartns_create_cq(struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector) {
    struct smartns_context *s_ctx = reinterpret_cast<smartns_context *>(context);

    cqe = std::bit_ceil(static_cast<uint32_t>(cqe));
    void *host_cq_buf = s_ctx->host_mr_allocator->alloc(cqe * sizeof(struct smartns_cqe), PAGE_SIZE);
    void *host_cq_doorbell = s_ctx->host_mr_allocator->alloc(sizeof(struct smartns_cq_doorbell), sizeof(struct smartns_cq_doorbell));
    void *bf_cq_buf = s_ctx->bf_mr_allocator->alloc(cqe * sizeof(struct smartns_cqe), PAGE_SIZE);
    void *bf_cq_doorbell = s_ctx->bf_mr_allocator->alloc(sizeof(struct smartns_cq_doorbell), sizeof(struct smartns_cq_doorbell));
    assert(host_cq_buf != nullptr);
    assert(host_cq_doorbell != nullptr);
    assert(bf_cq_buf != nullptr);
    assert(bf_cq_doorbell != nullptr);
    assert(reinterpret_cast<size_t>(host_cq_buf) % PAGE_SIZE == 0);
    assert(reinterpret_cast<size_t>(bf_cq_buf) % PAGE_SIZE == 0);

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

    s_cq->wqe_size = sizeof(struct smartns_cqe);
    s_cq->wqe_cnt = cqe;
    s_cq->wqe_shift = std::log2(s_cq->wqe_size);
    s_cq->head = 0;
    s_cq->own_flag = 1;

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

    // use sq_sig_all as datapath_send_wq_id
    assert(static_cast<size_t>(qp_init_attr->sq_sig_all) < s_ctx->send_wq_list.size());

    uint32_t		max_send_wr = qp_init_attr->cap.max_send_wr;
    uint32_t		max_recv_wr = qp_init_attr->cap.max_recv_wr;
    uint32_t		max_send_sge = qp_init_attr->cap.max_send_sge;
    uint32_t		max_recv_sge = qp_init_attr->cap.max_recv_sge;
    uint32_t		max_inline_data = qp_init_attr->cap.max_inline_data;
    enum ibv_qp_type	qp_type = qp_init_attr->qp_type;

    // only support max_send_sge == 1 at now
    assert(max_send_sge == 1);
    // don't support inline data at now
    assert(max_inline_data == 0);

    uint32_t recv_wqe_size = max_(1, max_recv_sge) * sizeof(smartns_recv_wqe);
    recv_wqe_size = std::bit_ceil(recv_wqe_size);

    uint32_t send_wqe_cnt = std::bit_ceil(max_send_wr);
    uint32_t recv_wqe_cnt = std::bit_ceil(max_recv_wr);
    uint32_t recv_wq_size = recv_wqe_cnt * recv_wqe_size;

    void *host_recv_wq_addr = s_ctx->host_mr_allocator->alloc(recv_wq_size, PAGE_SIZE);
    void *bf_recv_wq_addr = s_ctx->bf_mr_allocator->alloc(recv_wq_size, PAGE_SIZE);
    assert(reinterpret_cast<size_t>(host_recv_wq_addr) % PAGE_SIZE == 0);
    assert(reinterpret_cast<size_t>(bf_recv_wq_addr) % PAGE_SIZE == 0);


    struct SMARTNS_CREATE_QP_PARAMS params;
    memset(&params, 0, sizeof(params));

    params.context_number = s_ctx->context_number;
    params.pd_number = s_pd->pd_number;
    params.datapath_send_wq_id = qp_init_attr->sq_sig_all;
    params.recv_wq_size = recv_wq_size;
    params.host_recv_wq_addr = host_recv_wq_addr;
    params.bf_recv_wq_addr = bf_recv_wq_addr;
    params.send_cq_number = send_cq->cq_number;
    params.recv_cq_number = recv_cq->cq_number;
    params.max_send_wr = send_wqe_cnt;
    params.max_recv_wr = recv_wqe_cnt;
    params.max_send_sge = max_send_sge;
    params.max_recv_sge = recv_wqe_size / sizeof(smartns_recv_wqe);
    params.max_inline_data = 0;
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
    s_qp->max_send_wr = send_wqe_cnt;
    s_qp->max_recv_wr = recv_wqe_cnt;
    s_qp->max_send_sge = max_send_sge;
    s_qp->max_recv_sge = recv_wqe_size / sizeof(smartns_recv_wqe);
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

    s_qp->recv_wq->wqe_size = recv_wqe_size;
    s_qp->recv_wq->wqe_cnt = recv_wqe_cnt;
    s_qp->recv_wq->wqe_shift = std::log2(recv_wqe_size);
    s_qp->recv_wq->max_sge = max_(1, max_recv_sge);
    s_qp->recv_wq->head = 0;
    s_qp->recv_wq->tail = 0;
    s_qp->recv_wq->own_flag = 1;
    s_qp->recv_wq->wrid = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t) * s_qp->recv_wq->wqe_cnt));

    s_qp->recv_wq->dma_wq.dma_send_recv_cq = create_dma_cq(s_ctx->context, 256);
    s_qp->recv_wq->dma_wq.start_index = 0;
    s_qp->recv_wq->dma_wq.dma_index = 0;
    s_qp->recv_wq->dma_wq.finish_index = 0;
    s_qp->recv_wq->dma_wq.max_num = 256;
    // 16 * 4 = 64B
    s_qp->recv_wq->dma_wq.dma_batch_size = 4;
    s_qp->recv_wq->dma_wq.host_addr = s_qp->recv_wq->host_recv_wq_buf;
    s_qp->recv_wq->dma_wq.bf_addr = s_qp->recv_wq->bf_recv_wq_buf;
    s_qp->recv_wq->dma_wq.wqe_size = recv_wqe_size;
    s_qp->recv_wq->dma_wq.wqe_cnt = recv_wqe_cnt;

    s_qp->recv_wq->dma_wq.dma_qp = create_dma_qp(s_ctx->context, s_ctx->inner_pd, s_qp->recv_wq->dma_wq.dma_send_recv_cq, s_qp->recv_wq->dma_wq.dma_send_recv_cq, 256);
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
    free(s_qp->recv_wq->wrid);

    ibv_destroy_qp(s_qp->recv_wq->dma_wq.dma_qp);
    ibv_destroy_cq(s_qp->recv_wq->dma_wq.dma_send_recv_cq);
    delete s_qp->recv_wq;
    delete s_qp;

    return 0;
}


int smartns_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr) {
    struct smartns_qp *s_qp = reinterpret_cast<smartns_qp *>(qp);

    struct smartns_send_wqe *scat;
    int nreq;
    int ind;

    s_qp->send_wq->lock.lock();
    ind = s_qp->send_wq->head & (s_qp->send_wq->wqe_cnt - 1);

    for (nreq = 0;wr;++nreq, wr = wr->next) {
        if (unlikely(s_qp->send_wq->head - s_qp->send_wq->tail + nreq >= s_qp->send_wq->wqe_cnt)) {
            fprintf(stderr, "Error, post send wq full\n");
            exit(1);
        }
        if (unlikely(static_cast<uint32_t>(wr->num_sge) > s_qp->max_send_sge)) {
            fprintf(stderr, "Error, post send sge too many\n");
            exit(1);
        }

        scat = reinterpret_cast<struct smartns_send_wqe *>(reinterpret_cast<uint8_t *>(s_qp->send_wq->host_send_wq_buf) + (ind << s_qp->send_wq->wqe_shift));
        scat->qpn = s_qp->qp_number;
        scat->opcode = wr->opcode;
        scat->imm = 0;
        scat->local_addr = wr->sg_list[0].addr;

        assert(s_qp->context->mr_list.count(wr->sg_list[0].lkey));
        scat->local_lkey = s_qp->context->mr_list[wr->sg_list[0].lkey]->bf_mkey;
        scat->byte_count = wr->sg_list[0].length;

        if (wr->opcode == IBV_WR_RDMA_WRITE || wr->opcode == IBV_WR_RDMA_READ) {
            scat->remote_addr = wr->wr.rdma.remote_addr;
            scat->remote_rkey = wr->wr.rdma.rkey;
        }
        scat->cur_pos = s_qp->send_wq->head + nreq;
        scat->is_signal = wr->send_flags & IBV_SEND_SIGNALED;
        scat->op_own = s_qp->send_wq->own_flag;

        s_qp->send_wq->wrid[ind] = wr->wr_id;

        ind++;
        s_qp->send_wq->dma_wq.step_dma_req(s_qp->send_wq->bf_mr_lkey, s_qp->send_wq->host_mr_lkey);

        if (static_cast<uint32_t>(ind) == s_qp->send_wq->wqe_cnt) {
            s_qp->send_wq->dma_wq.flush_dma_req(s_qp->send_wq->bf_mr_lkey, s_qp->send_wq->host_mr_lkey);
            ind = 0;
            s_qp->send_wq->own_flag = s_qp->send_wq->own_flag ^ SMARTNS_SEND_WQE_OWNER_MASK;
        }
    }
    if (nreq) {
        s_qp->send_wq->head += nreq;
    }

    s_qp->send_wq->dma_wq.poll_dma_cq();

    s_qp->send_wq->lock.unlock();
    return 0;
}

int smartns_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr) {
    struct smartns_qp *s_qp = reinterpret_cast<smartns_qp *>(qp);

    struct smartns_recv_wqe *scat;
    int nreq;
    int ind;
    int i, j;

    s_qp->recv_wq->lock.lock();
    ind = s_qp->recv_wq->head & (s_qp->recv_wq->wqe_cnt - 1);

    for (nreq = 0;wr;++nreq, wr = wr->next) {
        if (unlikely(s_qp->recv_wq->head - s_qp->recv_wq->tail + nreq >= s_qp->recv_wq->wqe_cnt)) {
            fprintf(stderr, "Error, post recv wq full\n");
            exit(1);
        }
        if (unlikely(static_cast<uint32_t>(wr->num_sge) > s_qp->max_recv_sge)) {
            fprintf(stderr, "Error, post recv sge too many\n");
            exit(1);
        }

        scat = reinterpret_cast<struct smartns_recv_wqe *>(reinterpret_cast<uint8_t *>(s_qp->recv_wq->host_recv_wq_buf) + (ind << s_qp->recv_wq->wqe_shift));
        for (i = 0, j = 0;i < wr->num_sge;++i) {
            scat[j].addr = wr->sg_list[i].addr;

            assert(s_qp->context->mr_list.count(wr->sg_list[i].lkey));
            scat[j].lkey = s_qp->context->mr_list[wr->sg_list[i].lkey]->bf_mkey;
            scat[j].byte_count = wr->sg_list[i].length;
            scat[j].op_own = s_qp->recv_wq->own_flag;
            j++;
        }
        if (static_cast<uint32_t>(j) < s_qp->recv_wq->max_sge) {
            scat[j].addr = 0;
            scat[j].lkey = 100;
            scat[j].byte_count = 0;
            scat[j].op_own = s_qp->recv_wq->own_flag;
        }

        s_qp->recv_wq->wrid[ind] = wr->wr_id;

        ind++;
        s_qp->recv_wq->dma_wq.step_dma_req(s_qp->recv_wq->bf_mr_lkey, s_qp->recv_wq->host_mr_lkey);
        if (static_cast<uint32_t>(ind) == s_qp->recv_wq->wqe_cnt) {
            s_qp->recv_wq->dma_wq.flush_dma_req(s_qp->recv_wq->bf_mr_lkey, s_qp->recv_wq->host_mr_lkey);
            ind = 0;
            s_qp->recv_wq->own_flag = s_qp->recv_wq->own_flag ^ SMARTNS_RECV_WQE_OWNER_MASK;
        }
    }
    if (nreq) {
        s_qp->recv_wq->head += nreq;
    }

    s_qp->recv_wq->dma_wq.poll_dma_cq();

    s_qp->recv_wq->lock.unlock();
    return 0;
}

int smartns_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc) {
    struct smartns_cq *s_cq = reinterpret_cast<smartns_cq *>(cq);
    struct smartns_context *s_ctx = s_cq->context;

    int npolled;

    s_cq->lock.lock();

    for (npolled = 0; npolled < num_entries;npolled++) {
        struct smartns_cqe *cqe = reinterpret_cast<struct smartns_cqe *>(reinterpret_cast<uint8_t *>(s_cq->host_cq_buf) + (s_cq->head & (s_cq->wqe_cnt - 1)) * s_cq->wqe_size);
        if (cqe->op_own != s_cq->own_flag) {
            break;
        }
        struct smartns_qp *qp = s_ctx->qp_list[cqe->qpn];
        assert(qp);
        switch (cqe->cq_opcode) {
        case MLX5_CQE_REQ: {
            wc->byte_len = cqe->byte_count;
            if (cqe->mlx5_opcode == IBV_WR_SEND) {
                wc->opcode = IBV_WC_SEND;
            } else if (cqe->mlx5_opcode == IBV_WR_RDMA_WRITE) {
                wc->opcode = IBV_WC_RDMA_WRITE;
            } else if (cqe->mlx5_opcode == IBV_WR_RDMA_READ) {
                wc->opcode = IBV_WC_RDMA_READ;
            } else {
                fprintf(stderr, "Not support %u mlx5 opcode now!\n", cqe->mlx5_opcode);
                exit(1);
            }
            wc->wc_flags = 0;

            uint16_t wqe_ctr = cqe->wqe_counter & (qp->send_wq->wqe_cnt - 1);
            wc->wr_id = qp->send_wq->wrid[wqe_ctr];
            wc->status = IBV_WC_SUCCESS;
            if (qp->send_wq->tail > cqe->wqe_counter) {
                printf("Warning, send wq tail %u, cqe wqe counter %u\n", qp->send_wq->tail, cqe->wqe_counter);

                if (cqe->wqe_counter < qp->send_wq->wqe_cnt && qp->send_wq->tail >= UINT32_MAX - qp->send_wq->wqe_cnt) {
                    qp->send_wq->tail = cqe->wqe_counter + 1;
                } else {
                    printf("Illegal send wq tail %u, cqe wqe counter %u\n", qp->send_wq->tail, cqe->wqe_counter);
                    exit(1);
                }
            } else {
                qp->send_wq->tail = cqe->wqe_counter + 1;
            }
            break;
        }


        case MLX5_CQE_RESP_SEND: {

            wc->byte_len = cqe->byte_count;
            wc->opcode = IBV_WC_RECV;
            wc->wc_flags = 0;

            uint16_t wqe_ctr = cqe->wqe_counter & (qp->recv_wq->wqe_cnt - 1);
            wc->wr_id = qp->recv_wq->wrid[wqe_ctr];
            wc->status = IBV_WC_SUCCESS;
            qp->recv_wq->tail++;
            break;
        }

        default:
            fprintf(stderr, "Not support %u cq opcode now!\n", cqe->cq_opcode);
            exit(1);
            break;
        }

        s_cq->head++;
        if (s_cq->head % s_cq->wqe_cnt == 0) {
            s_cq->own_flag = s_cq->own_flag ^ SMARTNS_CQE_OWNER_MASK;
        }
    }
    s_cq->host_cq_doorbell->consumer_index = s_cq->head;

    s_cq->lock.unlock();

    return npolled;
}