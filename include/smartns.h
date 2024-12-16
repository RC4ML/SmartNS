#pragma once
#include "common.hpp"
#include "config.h"
#include "offset_handler.h"
#include "numautil.h"
#include "rdma_cm/libr.h"
#include "tcp_cm/tcp_cm.h"
#include "smartns_abi.h"
#include "phmap.h"
#include "devx/devx_mr.h"


extern std::atomic<bool> stop_flag;

class alignas(64) dma_handler {

public:
    dma_handler(ibv_context *context, ibv_pd *pd);

    ~dma_handler();
    // context and pd will shared with TXPATH QP
    // don't direct free context and pd at here
    ibv_context *context;
    ibv_pd *pd;

    std::vector<ibv_qp *> dma_qp_list;
    std::vector<ibv_qp_ex *>dma_qpx_list;
    std::vector<mlx5dv_qp_ex *> dma_mqpx_list;
    std::vector<uint64_t> dma_start_index_list;
    std::vector<uint64_t> dma_finish_index_list;

    // this cq will be shared within all DMA QP
    ibv_cq *dma_send_cq;
    // useless for dma 
    ibv_cq *dma_recv_cq;
};

class alignas(64) txpath_handler {

public:
    txpath_handler(ibv_context *context, ibv_pd *pd, void *buf_addr, size_t send_buf_size);

    ~txpath_handler();

    ibv_context *context;
    ibv_pd *pd;

    ibv_mr *mr;
    ibv_cq *send_cq;
    ibv_qp *send_qp;
    ibv_sge *send_sge_list;
    ibv_send_wr *send_wr;
    ibv_send_wr *send_bad_wr;

    size_t num_wrs;
    size_t num_sges_per_wr;
    size_t num_sges;
    size_t tx_depth;
    // convert to size_t 
    size_t send_buf_addr;

    offset_handler send_offset_handler;
    offset_handler send_comp_offset_handler;

    // calculate rss and select special port for remote server 
    uint16_t src_port;
};

class alignas(64) rxpath_handler {

public:
    rxpath_handler(ibv_context *all_rx_context, ibv_pd *all_rx_pd, void *buf_addr, size_t recv_buf_size);

    ~rxpath_handler();

    ibv_context *context;
    ibv_pd *pd;

    /* Exclusive for each handler */
    ibv_mr *mr;
    ibv_cq *recv_cq;
    ibv_wq *recv_wq;
    ibv_sge *recv_sge_list;
    ibv_recv_wr *recv_wr;
    ibv_recv_wr *recv_bad_wr;

    size_t num_wrs;
    size_t num_sges_per_wr;
    size_t num_sges;
    size_t rx_depth;
    // convert to size_t 
    size_t recv_buf_addr;

    offset_handler recv_offset_handler;
    offset_handler recv_comp_offset_handler;
};

class alignas(64) datapath_handler {

public:
    size_t thread_id;
    size_t cpu_id;
    ::dma_handler *dma_handler;
    ::txpath_handler *txpath_handler;
    ::rxpath_handler *rxpath_handler;
};

class datapath_manager {
private:
    void create_raw_packet_main_qp();
    void create_main_flow();
public:
    datapath_manager(ibv_context *all_context, ibv_pd *all_pd, size_t numa_node, bool is_server);
    ~datapath_manager();

    bool is_server;
    size_t numa_node;

    std::vector<datapath_handler> datapath_handler_list;

    ibv_context *global_context;
    ibv_pd *global_pd;
    ibv_qp *main_rss_qp{ nullptr };
    ibv_rwq_ind_table *main_rwq_ind_table;
    size_t main_rss_size;
    ibv_flow *main_flow{ nullptr };

    std::vector<void *>txpath_send_buf_list;
    std::vector<void *>rxpath_recv_buf_list;
};


struct alignas(64) dpu_send_wq {
    struct dpu_context *dpu_ctx;
    void *bf_send_wq_buf;
    uint32_t max_num;
    uint32_t cur_num;
    size_t send_wq_id;
};

struct alignas(64) dpu_recv_wq {
    struct dpu_context *dpu_ctx;
    void *bf_recv_wq_buf;
    uint32_t max_num;
    uint32_t cur_num;
};

struct alignas(64) dpu_qp {
    struct dpu_context *dpu_ctx;
    struct dpu_pd *dpu_pd;
    size_t qp_number;
    size_t remote_qp_number;
    ibv_qp_type qp_type;
    unsigned int max_send_wr;
    unsigned int max_recv_wr;
    unsigned int max_send_sge;
    unsigned int max_recv_sge;
    unsigned int max_inline_data;

    struct dpu_cq *send_cq;
    struct dpu_cq *recv_cq;

    struct dpu_send_wq *send_wq;
    struct dpu_recv_wq *recv_wq;
};

struct dpu_pd {
    struct dpu_context *dpu_ctx;
    size_t pd_number;
};

struct alignas(64) dpu_cq {
    struct dpu_context *dpu_ctx;
    size_t cq_number;
    uint32_t max_num;
    uint32_t cur_num;
    void *host_cq_buf;
    void *host_cq_doorbell;
    void *bf_cq_buf;
    void *bf_cq_doorbell;
};

struct dpu_mr {
    unsigned int host_mkey;
    struct devx_mr *devx_mr;
};

struct dpu_context {
    int host_pid;
    int host_tgid;

    size_t context_number;
    struct devx_mr *inner_bf_mr;
    struct devx_mr *inner_host_mr;

    std::vector<dpu_send_wq> send_wq_list;

    // pdn to struct pd
    phmap::flat_hash_map<size_t, dpu_pd *>pd_list;
    // cqn to struct cq
    phmap::flat_hash_map<size_t, dpu_cq *>cq_list;
    // qpn to struct qp
    phmap::flat_hash_map<size_t, dpu_qp *>qp_list;
    // host mkey to mr
    phmap::flat_hash_map<unsigned int, dpu_mr *>mr_list;
};

class alignas(64) controlpath_manager {
private:
    size_t generate_context_number();
    size_t generate_pd_number();
    size_t generate_cq_number();
    size_t generate_qp_number();
public:

    const size_t control_packet_size = 512;
    const size_t control_tx_depth = 128;
    const size_t control_rx_depth = 128;
    controlpath_manager(std::string device_name, size_t numa_node, bool is_server);
    ~controlpath_manager();

    void handle_open_device(SMARTNS_OPEN_DEVICE_PARAMS *param);
    void handle_close_device(SMARTNS_CLOSE_DEVICE_PARAMS *param);
    void handle_alloc_pd(SMARTNS_ALLOC_PD_PARAMS *param);
    void handle_dealloc_pd(SMARTNS_DEALLOC_PD_PARAMS *param);
    void handle_reg_mr(SMARTNS_REG_MR_PARAMS *param);
    void handle_destory_mr(SMARTNS_DESTROY_MR_PARAMS *param);
    void handle_create_cq(SMARTNS_CREATE_CQ_PARAMS *param);
    void handle_destory_cq(SMARTNS_DESTROY_CQ_PARAMS *param);
    void handle_create_qp(SMARTNS_CREATE_QP_PARAMS *param);
    void handle_destory_qp(SMARTNS_DESTROY_QP_PARAMS *param);
    void handle_modify_qp(SMARTNS_MODIFY_QP_PARAMS *param);

    phmap::flat_hash_map<size_t, dpu_context *>context_list;

    ibv_context *global_context;
    ibv_pd *global_pd;

    size_t cpu_id;

    bool is_server;
    size_t numa_node;
    std::string device_name;

    rdma_param control_rdma_param;
    void *send_recv_buf;
    size_t send_recv_buf_size;
    pingpong_info local_bf_info;
    pingpong_info remote_host_info;
    qp_handler *control_qp_handler;
    offset_handler send_handler;
    offset_handler send_comp_handler;
    offset_handler recv_handler;
    offset_handler recv_comp_handler;

    tcp_param control_net_param;
};