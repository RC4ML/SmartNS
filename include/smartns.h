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
    uint32_t wqe_size;
    uint32_t wqe_cnt;
    uint32_t wqe_shift;
    uint32_t max_sge;
    uint32_t	head;

    uint32_t now_sge_num;
    uint32_t now_sge_offset;

    uint8_t own_flag;

    inline smartns_recv_wqe *get_next_wqe() {
        smartns_recv_wqe *wqe = reinterpret_cast<smartns_recv_wqe *>(reinterpret_cast<uint8_t *>(bf_recv_wq_buf) + (head << wqe_shift));
        assert(wqe->op_own == own_flag);
        SMARTNS_TRACE("recv wqe addr 0X%lx lkey %u byte count %u\n", wqe->addr, wqe->lkey, wqe->byte_count);

        return wqe;
    }
    inline void step_wq() {
        ++head;
        if (head == wqe_cnt) {
            head = 0;
            own_flag = own_flag ^ SMARTNS_RECV_WQE_OWNER_MASK;
        }
        now_sge_num = 0;
        now_sge_offset = 0;
    }
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
    uint32_t wqe_size;
    uint32_t wqe_cnt;
    uint32_t wqe_shift;
    uint32_t head;
    uint32_t tail;
    uint32_t bf_mkey;
    uint32_t host_mkey;

    void *host_cq_buf;
    smartns_cq_doorbell *host_cq_doorbell;
    void *bf_cq_buf;
    smartns_cq_doorbell *bf_cq_doorbell;

    uint8_t own_flag;
    inline smartns_cqe *get_next_cqe() {
        smartns_cqe *cqe = reinterpret_cast<smartns_cqe *>(reinterpret_cast<uint8_t *>(bf_cq_buf) + (head << wqe_shift));

        // head update will happend in post_dma_req_with_cq
        return cqe;
    }

    inline void step_cq() {
        ++head;
        if (head == wqe_cnt) {
            head = 0;
            own_flag = own_flag ^ SMARTNS_CQE_OWNER_MASK;
        }
    }
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

class alignas(64) dma_handler {

public:
    dma_handler(ibv_context *context, ibv_pd *pd);

    ~dma_handler();
    // context and pd will shared with TXPATH QP
    // don't direct free context and pd at here
    ibv_context *context;
    ibv_pd *pd;

    ibv_qp **dma_qp_list;
    ibv_qp_ex **dma_qpx_list;
    mlx5dv_qp_ex **dma_mqpx_list;
    //plus one when dma anything, and reset to 0 when set signal
    uint64_t *dma_count_list;
    //plus one when dma packet payload, and reset to 0 when set signal
    uint64_t *payload_count_list;

    ibv_qp **invalid_qp_list;
    ibv_qp_ex **invalid_qpx_list;
    mlx5dv_qp_ex **invalid_mqpx_list;
    uint64_t *invalid_start_index_list;
    uint64_t *invalid_finish_index_list;

    uint32_t now_use_qp_index;
    uint32_t qp_group_size;

    // this cq will be shared within all DMA QP
    ibv_cq *dma_send_recv_cq;
    // this cq will be shared within all cache invalid QP
    ibv_cq *invalid_send_recv_cq;

    inline void post_dma_req_without_cq(uint32_t dest_lkey, uint64_t dest_addr,
        uint32_t src_lkey, uint64_t src_addr, size_t length) {
        bool is_signal = dma_count_list[now_use_qp_index] % SMARTNS_DMA_BATCH == (SMARTNS_DMA_BATCH - 1);

        dma_count_list[now_use_qp_index]++;
        payload_count_list[now_use_qp_index]++;

        dma_qpx_list[now_use_qp_index]->wr_id = now_use_qp_index | (payload_count_list[now_use_qp_index] << 32);
        dma_qpx_list[now_use_qp_index]->wr_flags = is_signal ? IBV_SEND_SIGNALED : 0;
        dma_mqpx_list[now_use_qp_index]->wr_memcpy_direct(dma_mqpx_list[now_use_qp_index], dest_lkey, dest_addr, src_lkey, src_addr, length);

        bool is_invalid_signal = invalid_start_index_list[now_use_qp_index] % 16 == 15;
        invalid_qpx_list[now_use_qp_index]->wr_id = 0;
        invalid_qpx_list[now_use_qp_index]->wr_flags = is_invalid_signal ? IBV_SEND_SIGNALED : 0;

        invalid_mqpx_list[now_use_qp_index]->wr_invcache_direct_prefill(invalid_mqpx_list[now_use_qp_index], src_lkey, src_addr, length, false);
        invalid_start_index_list[now_use_qp_index]++;

        if (is_signal) {
            dma_count_list[now_use_qp_index] = 0;
            payload_count_list[now_use_qp_index] = 0;
            now_use_qp_index = (now_use_qp_index + 1) % qp_group_size;
        }
    }

    inline void post_dma_req_with_cq(uint32_t dest_lkey, uint64_t dest_addr,
        uint32_t src_lkey, uint64_t src_addr, size_t length, dpu_cq *cq) {
        bool is_signal = dma_count_list[now_use_qp_index] % SMARTNS_DMA_BATCH >= (SMARTNS_DMA_BATCH - 2);

        dma_count_list[now_use_qp_index] += 2;
        payload_count_list[now_use_qp_index]++;

        dma_qpx_list[now_use_qp_index]->wr_id = now_use_qp_index | (payload_count_list[now_use_qp_index] << 32);
        // will signal at cq dma
        dma_qpx_list[now_use_qp_index]->wr_flags = 0;
        dma_mqpx_list[now_use_qp_index]->wr_memcpy_direct(dma_mqpx_list[now_use_qp_index], dest_lkey, dest_addr, src_lkey, src_addr, length);

        dma_qpx_list[now_use_qp_index]->wr_id = now_use_qp_index | (payload_count_list[now_use_qp_index] << 32);
        dma_qpx_list[now_use_qp_index]->wr_flags = is_signal ? IBV_SEND_SIGNALED : 0;
        size_t cqe_offset = (cq->head & (cq->wqe_cnt - 1)) << cq->wqe_shift;
        dma_mqpx_list[now_use_qp_index]->wr_memcpy_direct(dma_mqpx_list[now_use_qp_index], cq->host_mkey, reinterpret_cast<uint64_t>(cq->host_cq_buf) + cqe_offset, cq->bf_mkey, reinterpret_cast<uint64_t>(cq->bf_cq_buf) + cqe_offset, cq->wqe_size);

        bool is_invalid_signal = invalid_start_index_list[now_use_qp_index] % 16 == 15;
        invalid_qpx_list[now_use_qp_index]->wr_id = 0;
        invalid_qpx_list[now_use_qp_index]->wr_flags = is_invalid_signal ? IBV_SEND_SIGNALED : 0;

        invalid_mqpx_list[now_use_qp_index]->wr_invcache_direct_prefill(invalid_mqpx_list[now_use_qp_index], src_lkey, src_addr, length, false);
        invalid_start_index_list[now_use_qp_index]++;

        if (is_signal) {
            dma_count_list[now_use_qp_index] = 0;
            payload_count_list[now_use_qp_index] = 0;
            now_use_qp_index = (now_use_qp_index + 1) % qp_group_size;
        }
    }

    inline void post_only_cq(dpu_cq *cq) {
        bool is_signal = dma_count_list[now_use_qp_index] % SMARTNS_DMA_BATCH == (SMARTNS_DMA_BATCH - 1);

        dma_count_list[now_use_qp_index]++;

        dma_qpx_list[now_use_qp_index]->wr_id = now_use_qp_index | (payload_count_list[now_use_qp_index] << 32);
        dma_qpx_list[now_use_qp_index]->wr_flags = is_signal ? IBV_SEND_SIGNALED : 0;
        size_t cqe_offset = (cq->head & (cq->wqe_cnt - 1)) << cq->wqe_shift;
        dma_mqpx_list[now_use_qp_index]->wr_memcpy_direct(dma_mqpx_list[now_use_qp_index], cq->host_mkey, reinterpret_cast<uint64_t>(cq->host_cq_buf) + cqe_offset, cq->bf_mkey, reinterpret_cast<uint64_t>(cq->bf_cq_buf) + cqe_offset, cq->wqe_size);

        if (is_signal) {
            dma_count_list[now_use_qp_index] = 0;
            payload_count_list[now_use_qp_index] = 0;
            now_use_qp_index = (now_use_qp_index + 1) % qp_group_size;
        }
    }

    inline uint32_t poll_dma_cq() {
        uint32_t total_finish_dma = 0;
        ibv_wc wc[16];
        for (uint32_t i = 0;i < qp_group_size;i++) {
            uint32_t num_wc = ibv_poll_cq(dma_send_recv_cq, 16, wc);
            for (uint32_t j = 0; j < num_wc; j++) {
                assert(wc[i].status == IBV_WC_SUCCESS);
                uint64_t wr_id = wc[i].wr_id;
                uint32_t qp_index = wr_id & 0xFFFFFFFF;
                uint32_t payload_count = wr_id >> 32;
                invalid_mqpx_list[qp_index]->wr_invcache_direct_flush(invalid_mqpx_list[qp_index], invalid_finish_index_list[qp_index], payload_count);
                invalid_finish_index_list[qp_index] += payload_count;
                total_finish_dma += payload_count;
            }
        }

        for (uint32_t i = 0;i < qp_group_size;i++) {
            uint32_t num_wc = ibv_poll_cq(invalid_send_recv_cq, 16, wc);
            for (uint32_t j = 0; j < num_wc; j++) {
                assert(wc[i].status == IBV_WC_SUCCESS);
            }
        }

        return total_finish_dma;
    }
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
    struct ibv_wc *wc_send_recv;

    phmap::flat_hash_map<uint64_t, dpu_qp *>local_qpn_to_qp_list;
    phmap::flat_hash_map<uint64_t, dpu_qp *>remote_qpn_to_qp_list;

    size_t handle_recv();

    void dma_payload_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size);
    // need create cq before use this function
    void dma_payload_with_cq_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size);
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

    datapath_manager *data_manager;
};