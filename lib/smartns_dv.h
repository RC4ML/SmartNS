#pragma once

#include "common.hpp"
#include "allocator.h"
#include "devx_mr.h"
#include "spinlock_mutex.h"
#include "offset_handler.h"
#include "phmap.h"
#include "page.h"
#include <sys/ioctl.h>


#define SMARTNS_CONTEXT_ALLOC_SIZE (8*1024*1024)
static_assert(is_log2(SMARTNS_CONTEXT_ALLOC_SIZE));

static uint8_t vhca_access_key[32] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1
};

struct smartns_send_wqe {
    char a[64];
};
static_assert(sizeof(smartns_send_wqe) == 64);

struct smartns_recv_wqe {
    char a[16];
};
static_assert(sizeof(smartns_recv_wqe) == 16);


struct smartns_cqe {
    char a[64];
};
static_assert(sizeof(smartns_cqe) == 64);

struct smartns_cq_doorbell {
    size_t consumer_index;
    char padding[56];
};
static_assert(sizeof(smartns_cq_doorbell) == 64);

struct smartns_dma_wq {
    struct ibv_qp *dma_qp;
    struct ibv_qp_ex *dma_qpx;
    struct mlx5dv_qp_ex *dma_mqpx;
    uint32_t start_index;
    uint32_t finish_index;
    uint32_t max_num;

    ibv_cq *dma_send_cq;
    ibv_cq *dma_recv_cq;
};

struct alignas(64) smartns_send_wq {
    size_t send_wq_id;
    uint32_t host_mr_lkey;
    uint32_t bf_mr_lkey;

    void *host_send_wq_buf;
    void *bf_send_wq_buf;

    uint32_t max_num;
    uint32_t cur_num;

    struct smartns_dma_wq dma_wq;
    spinlock_mutex lock;
};

struct alignas(64) smartns_recv_wq {
    uint32_t host_mr_lkey;
    uint32_t bf_mr_lkey;

    void *host_recv_wq_buf;
    void *bf_recv_wq_buf;

    uint32_t max_num;
    uint32_t max_sge;
    uint32_t cur_num;

    struct smartns_dma_wq dma_wq;
    spinlock_mutex lock;
};


struct smartns_qp {
    ibv_qp *qp;
    struct smartns_context *context;
    struct smartns_pd *pd;

    struct smartns_send_wq *send_wq;
    struct smartns_recv_wq *recv_wq;

    struct smartns_cq *send_cq;
    struct smartns_cq *recv_cq;

    uint32_t		max_send_wr;
    uint32_t		max_recv_wr;
    uint32_t		max_send_sge;
    uint32_t		max_recv_sge;
    uint32_t		max_inline_data;
    enum ibv_qp_type	qp_type;

    // generate from bf
    size_t qp_number;
    size_t remote_qp_number;
    enum ibv_qp_state	cur_qp_state;
};

// donothing for now
struct smartns_pd {
    struct ibv_pd *pd;
    // generate from bf
    size_t pd_number;
    struct smartns_context *context;
};


struct smartns_cq {
    struct ibv_cq *cq;

    struct smartns_context *context;
    // generate from bf
    size_t cq_number;

    void *host_cq_buf;
    smartns_cq_doorbell *host_cq_doorbell;
    // bf_cq_buf/bf_cq_doorbell can't direct load/store, just record
    void *bf_cq_buf;
    smartns_cq_doorbell *bf_cq_doorbell;
    uint32_t max_num;
    uint32_t cur_num;
};

struct smartns_mr {
    ibv_mr mr;
    devx_mr *devx_mr;
};

struct smartns_context {
    struct ibv_context *context;
    // used for inner memory region(cq and others)
    struct ibv_pd *inner_pd;

    // host_mr use buddy_allocator->buddy_base_address address
    struct devx_mr *inner_host_mr;
    struct devx_mr *inner_bf_mr;

    void *host_mr_base;
    custom_allocator *host_mr_allocator;
    custom_allocator *bf_mr_allocator;

    int kernel_fd;

    size_t context_number;

    std::vector<smartns_send_wq> send_wq_list;
    // qpn to struct qp
    phmap::flat_hash_map<size_t, smartns_qp *>qp_list;
    // pdn to struct pd
    phmap::flat_hash_map<size_t, smartns_pd *>pd_list;
    // cqn to struct cq
    phmap::flat_hash_map<size_t, smartns_cq *>cq_list;
};

struct ibv_context *smartns_open_device(struct ibv_device *ib_dev);

int smartns_close_device(struct ibv_context *context);

struct ibv_pd *smartns_alloc_pd(struct ibv_context *context);

int smartns_dealloc_pd(struct ibv_pd *pd);

struct ibv_mr *smartns_reg_mr(struct ibv_pd *pd, void *addr, size_t length, unsigned int access);

int smartns_dereg_mr(struct ibv_mr *mr);

struct ibv_cq *smartns_create_cq(struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector);

int smartns_destroy_cq(struct ibv_cq *cq);

ibv_qp *smartns_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);

int smartns_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);

int smartns_destroy_qp(struct ibv_qp *qp);

int smartns_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);

int smartns_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr);

int smartns_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);





