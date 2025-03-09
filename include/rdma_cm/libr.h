#pragma once

#include "common.hpp"
#include "page.h"
#include "offset_handler.h"
#include "config.h"

#define RDMA_TX_DEPTH (1024)
#define RDMA_RX_DEPTH (1024)
#define RDMA_MAX_OUT_READ (1)
#define RDMA_IB_PORT (1)
#define RDMA_HOST_GID_INDEX (3)
#define RDMA_BF_GID_INDEX (1)
#define RDMA_MAX_INLINE_SIZE (0)

#define MIN_RNR_TIMER	(12)
#define DEF_QP_TIME     (14)
#define CTX_POLL_BATCH	(16)
#define SEND_CQ_BATCH   (8)
#define INFO_FMT "LID %#04x QPN %#06x PSN %#08x RKey %#08x VAddr %#016llx  %s: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d"

/**
 * @struct pingpong_info
 * @brief Structure to hold information for RDMA ping-pong communication.
 *
 * This structure contains various fields required for setting up and managing
 * RDMA (Remote Direct Memory Access) communication, specifically for ping-pong
 * style messaging.
 *
 * This information needs to be exchanged between the client and server to establish
 * a connection. Used by connect_qp_rc.
 *
 * @see connect_qp_rc
 */
struct pingpong_info {
    int					lid; /**< Local Identifier (LID) for the RDMA connection. */
    int 				qpn; /**< Queue Pair Number (QPN) for the RDMA connection. */
    int 				psn; /**< Packet Sequence Number (PSN) for the RDMA connection. */
    unsigned			rkey; /**< Remote key for accessing remote memory. */
    unsigned long long 	vaddr; /**< Virtual address of the remote memory. */
    unsigned char	raw_gid[16]; /**< Global Identifier (GID) for the RDMA connection, represented as a raw byte array. */
    unsigned char mac[6]; /**< MAC address, only used for kernel queue pairs. */
    int					gid_index; /**< Index of the GID in the GID table. */
    int					out_reads; /**< Number of outstanding reads. */
    int                 mtu;
};

/**
 * @struct rdma_param
 * @brief Structure to hold parameters for RDMA communication.
 *
 * This structure contains various fields required for setting up and managing
 * RDMA (Remote Direct Memory Access) communication.
 *
 * @see roce_init
 */
struct rdma_param {
    std::string device_name;  /**< Name of the RDMA device. */
    uint8_t ib_port;          /**< Port number of the RDMA device. */
    int gid_index;            /**< Index of the GID in the GID table. */
    int tx_depth;             /**< Depth of the transmit queue. */
    int rx_depth;             /**< Depth of the receive queue. */
    int max_out_read;         /**< Maximum number of outstanding reads. */
    uint32_t max_inline_size; /**< Maximum size of inline data. */
    int numa_node;            /**< NUMA node for memory allocation. */
    int batch_size;           /**< Batch size for operations. */
    int sge_per_wr;           /**< Number of scatter-gather elements per work request. */
    bool use_devx_context;    /**< Flag to indicate if DevX context is used. */

    // following param init by roce_init
    enum ibv_mtu cur_mtu;          /**< Current MTU size. */
    struct ibv_context **contexts; /**< Array of RDMA device contexts. */
    int num_contexts;              /**< Number of RDMA device contexts. */

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

qp_handler *create_qp_raw_packet(rdma_param &rdma_param, void *buf, size_t size, uint32_t tx_depth, uint32_t rx_depth, int context_index);

void init_wr_base_send_recv(qp_handler &qp_handler);

void init_wr_base_write(qp_handler &qp_handler);

void init_wr_base_read(qp_handler &qp_handler);

void print_pingpong_info(struct pingpong_info *info);

void post_send(qp_handler &qp_handler, size_t offset, int length);

void post_send_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

void post_recv(qp_handler &qp_handler, size_t offset, int length);

void post_recv_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

int poll_send_cq(qp_handler &qp_handler, struct ibv_wc *wc);

int poll_recv_cq(qp_handler &qp_handler, struct ibv_wc *wc);
