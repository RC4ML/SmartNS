#pragma once
#include "common.hpp"
#include "config.h"
#include "offset_handler.h"
extern std::atomic<bool> stop_flag;

class alignas(64) dma_handler {

public:
    dma_handler(ibv_context *context, ibv_pd *pd, void *buf_addr, size_t recv_buf_size);

    ~dma_handler();
    // context and pd will shared with TXPATH QP
    // don't direct free context and pd at here
    ibv_context *context;
    ibv_pd *pd;

    // need register memory region again, under the txpath context and pd
    ibv_mr *rxpath_recv_buf_mr;

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
    /* Exclusive for each handler */
    // context and pd will shared with DMA QP
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
};

class alignas(64) rxpath_handler {

public:
    rxpath_handler(ibv_context *all_rx_context, ibv_pd *all_rx_pd, void *buf_addr, size_t recv_buf_size);

    ~rxpath_handler();
    // this context used for all rxpath_handler
    ibv_context *context;
    // this pd used for all rxpath_handler
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
    ::dma_handler *dma_handler;
    ::txpath_handler *txpath_handler;
    ::rxpath_handler *rxpath_handler;
};

class datapath_manager {
private:
    void create_raw_packet_main_qp();
    void create_main_flow();
public:
    datapath_manager(std::string device_name, size_t numa_node, bool is_server);
    ~datapath_manager();

    bool is_server;
    size_t numa_node;
    std::string device_name;

    std::vector<datapath_handler> datapath_handler_list;


    ibv_context *all_rx_context;
    ibv_pd *all_rx_pd;
    ibv_qp *main_rss_qp;
    ibv_rwq_ind_table *main_rwq_ind_table;
    ibv_flow *main_flow;

    std::vector<void *>txpath_send_buf_list;
    std::vector<void *>rxpath_recv_buf_list;
};
