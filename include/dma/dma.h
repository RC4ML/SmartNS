#pragma once

#include "common.hpp"

ibv_qp *create_dma_qp(struct ibv_context *ibv_ctx,
    struct ibv_pd *pd, struct ibv_cq *rq_cq, struct ibv_cq *sq_cq, size_t depth);

ibv_cq *create_dma_cq(ibv_context *ibv_ctx, size_t depth);

void init_dma_qp(ibv_qp *qp);

void dma_qp_self_connected(ibv_qp *qp);

uint32_t get_mmo_dma_max_length(struct ibv_context *ibv_ctx);