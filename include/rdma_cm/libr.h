#pragma once

#include "common.hpp"
#include "page.h"
#include "offset_handler.h"
#include "config.h"

#define RDMA_TX_DEPTH (128)
#define RDMA_RX_DEPTH (128)
#define RDMA_MAX_OUT_READ (1)
#define RDMA_IB_PORT (1)
#define RDMA_HOST_GID_INDEX (3)
#define RDMA_BF_GID_INDEX (1)
#define RDMA_MAX_INLINE_SIZE (0)

#define MIN_RNR_TIMER	(12)
#define DEF_QP_TIME     (14)
#define CTX_POLL_BATCH	(16)
#define SEND_CQ_BATCH   (32)
#define INFO_FMT "LID %#04x QPN %#06x PSN %#08x RKey %#08x VAddr %#016llx  %s: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d"

struct pingpong_info {
    int					lid;
    int 				qpn;
    int 				psn;
    unsigned			rkey;
    unsigned long long 	vaddr;
    unsigned char	raw_gid[16];
    // only used for kernel qp
    unsigned char mac[6];
    int					gid_index;
    int					out_reads;
};

struct rdma_param {
    std::string 				device_name;
    uint8_t						ib_port;
    int							gid_index;
    int tx_depth;
    int rx_depth;
    int max_out_read;
    uint32_t max_inline_size;
    int 						numa_node;
    int 						batch_size;
    int 						sge_per_wr;
    bool use_devx_context;

    // following param init by roce_init
    enum ibv_mtu				cur_mtu;
    struct ibv_context **contexts;
    int							num_contexts;
    rdma_param() {
        ib_port = RDMA_IB_PORT;
#if defined(__x86_64__)
        gid_index = RDMA_HOST_GID_INDEX;
#elif defined(__aarch64__)
        gid_index = RDMA_BF_GID_INDEX;
#endif
        tx_depth = RDMA_TX_DEPTH;
        rx_depth = RDMA_RX_DEPTH;
        max_out_read = RDMA_MAX_OUT_READ;
        max_inline_size = RDMA_MAX_INLINE_SIZE;
        numa_node = 0;
        batch_size = 1;
        sge_per_wr = 1;
        use_devx_context = false;
    }
};

class qp_handler {
public:
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_qp *qp;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_sge *send_sge_list;
    struct ibv_sge *recv_sge_list;
    struct ibv_send_wr *send_wr;
    struct ibv_recv_wr *recv_wr;
    struct ibv_send_wr *send_bar_wr;
    struct ibv_recv_wr *recv_bar_wr;
    size_t buf;
    size_t remote_buf;
    unsigned int remote_rkey;
    int max_inline_size;
    int num_wrs;
    int num_sges_per_wr;
    int num_sges;
    int tx_depth;
    int rx_depth;
};
struct ibv_device *ctx_find_dev(char const *ib_devname);

struct ibv_context *ctx_open_device(struct ibv_device *ib_dev);

const char *transport_type_str(enum ibv_transport_type t);

const char *link_layer_str(int8_t link_layer);

void roce_init(rdma_param &rdma_param, int num_contexts);

qp_handler *create_qp_rc(rdma_param &rdma_param, void *buf, size_t size, struct pingpong_info *info, int context_index);

void connect_qp_rc(rdma_param &rdma_param, qp_handler &qp_handler, struct pingpong_info *remote_info, struct pingpong_info *local_info);

void init_wr_base_send_recv(qp_handler &qp_handler);

void init_wr_base_write(qp_handler &qp_handler);

void print_pingpong_info(struct pingpong_info *info);

void post_send(qp_handler &qp_handler, size_t offset, int length);

void post_send_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

void post_recv(qp_handler &qp_handler, size_t offset, int length);

void post_recv_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

int poll_send_cq(qp_handler &qp_handler, struct ibv_wc *wc);

int poll_recv_cq(qp_handler &qp_handler, struct ibv_wc *wc);
