#include "dma/dma.h"
#include "config.h"
ibv_qp *create_dma_qp(struct ibv_context *ibv_ctx,
    struct ibv_pd *pd, struct ibv_cq *rq_cq, struct ibv_cq *sq_cq, size_t depth) {
    struct ibv_qp_cap qp_cap = {
    .max_send_wr = static_cast<uint32_t>(depth),
    .max_recv_wr = static_cast<uint32_t>(depth),
    .max_send_sge = 1,
    .max_recv_sge = 1,
    .max_inline_data = 0
    };

    struct ibv_qp_init_attr_ex init_attr = {
        .qp_context = NULL,
        .send_cq = sq_cq,
        .recv_cq = rq_cq,
        .cap = qp_cap,
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 1,

        .comp_mask = IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_SEND_OPS_FLAGS,
        .pd = pd,
        .send_ops_flags = IBV_QP_EX_WITH_RDMA_WRITE | IBV_QP_EX_WITH_RDMA_WRITE_WITH_IMM | \
                          IBV_QP_EX_WITH_SEND | IBV_QP_EX_WITH_SEND_WITH_IMM | IBV_QP_EX_WITH_RDMA_READ,
    };

    struct mlx5dv_qp_init_attr attr_dv = {
        .comp_mask = MLX5DV_QP_INIT_ATTR_MASK_SEND_OPS_FLAGS,
        .send_ops_flags = MLX5DV_QP_EX_WITH_MEMCPY,
    };

    struct ibv_qp *qp = mlx5dv_create_qp(ibv_ctx, &init_attr, &attr_dv);
    if (NULL == qp) {
        SMARTNS_ERROR("failed to create qp\n");
        exit(__LINE__);
    }

    struct ibv_qp_attr qpa = {};
    struct ibv_qp_init_attr qpia = {};
    if (ibv_query_qp(qp, &qpa, IBV_QP_CAP, &qpia)) {
        SMARTNS_ERROR("failed to query qp cap\n");
        exit(__LINE__);
    }

    SMARTNS_INFO("create qp with qpn = 0x%x, max_send_wr = 0x%x\n", qp->qp_num, qpa.cap.max_send_wr);

    return qp;
}

ibv_cq *create_dma_cq(ibv_context *ibv_ctx, size_t depth) {
    struct ibv_cq_init_attr_ex cq_attr = {
    .cqe = static_cast<uint32_t>(depth),
    .cq_context = NULL,
    .channel = NULL,
    .comp_vector = 0
    };

    struct ibv_cq_ex *cq_ex = mlx5dv_create_cq(ibv_ctx, &cq_attr, NULL);
    if (NULL == cq_ex) {
        SMARTNS_ERROR("failed to create cq\n");
        exit(__LINE__);
    }

    return ibv_cq_ex_to_cq(cq_ex);
}

void init_dma_qp(ibv_qp *qp) {
    int mask = IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS;
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ,
        .pkey_index = 0,
        .port_num = 1,
    };

    if (ibv_modify_qp(qp, &attr, mask)) {
        SMARTNS_ERROR("failed to modify qp:0x%x to init\n", qp->qp_num);
        exit(__LINE__);
    }
}

void dma_qp_self_connected(ibv_qp *qp) {
    ibv_gid gid = {};

    if (ibv_query_gid(qp->context, 1, SMARTNS_DMA_GID_INDEX, &gid)) {
        SMARTNS_ERROR("failed to query port gid\n");
        exit(__LINE__);
    }

    int mask = IBV_QP_STATE | IBV_QP_AV | \
        IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | \
        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    struct ibv_qp_attr qpa = {
        .qp_state = IBV_QPS_RTR,
        .path_mtu = IBV_MTU_1024,
        .rq_psn = 0,
        .dest_qp_num = qp->qp_num,
        .ah_attr = {
            .grh = {
                .dgid = gid,
                .sgid_index = static_cast<uint8_t>(SMARTNS_DMA_GID_INDEX) ,
                .hop_limit = 64,
            },
            .is_global = 1,
            .port_num = 1,
        },
        .max_dest_rd_atomic = 1,
        .min_rnr_timer = 0x12,
    };

    if (ibv_modify_qp(qp, &qpa, mask)) {
        SMARTNS_ERROR("failed to modify qp:0x%x to rtr, errno 0x%x\n", qp->qp_num, errno);
        exit(__LINE__);
    }

    qpa.qp_state = IBV_QPS_RTS;
    qpa.timeout = 12;
    qpa.retry_cnt = 6;
    qpa.rnr_retry = 0;
    qpa.sq_psn = 0;
    qpa.max_rd_atomic = 1;
    mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | \
        IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(qp, &qpa, mask)) {
        printf("failed to modify qp:0x%x to rts, errno 0x%x\n", qp->qp_num, errno);
        exit(__LINE__);
    }
}

uint32_t get_mmo_dma_max_length(struct ibv_context *ibv_ctx) {
    struct mlx5dv_context attrs_out = {};

    attrs_out.comp_mask = MLX5DV_CONTEXT_MASK_WR_MEMCPY_LENGTH;
    if (mlx5dv_query_device(ibv_ctx, &attrs_out)) {
        printf("failed to query mmo dma max length\n");
        exit(__LINE__);
    }

    if (attrs_out.comp_mask & MLX5DV_CONTEXT_MASK_WR_MEMCPY_LENGTH) {
        return attrs_out.max_wr_memcpy_length;
    }

    return 0;
}
