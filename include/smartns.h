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
#include "raw_packet/raw_packet.h"

extern std::atomic<bool> stop_flag;

struct alignas(64) dpu_datapath_send_wq {
    struct dpu_context *dpu_ctx;
    size_t datapath_send_wq_id;
    void *bf_datapath_send_wq_buf;

    uint32_t wqe_size;
    uint32_t wqe_cnt;
    uint32_t wqe_shift;
    uint32_t head;

    uint8_t own_flag;

    smartns_send_wqe *get_next_wqe() {
        smartns_send_wqe *wqe = reinterpret_cast<smartns_send_wqe *>(reinterpret_cast<uint8_t *>(bf_datapath_send_wq_buf) + (head << wqe_shift));
        if (wqe->op_own != own_flag) {
            return nullptr;
        }
        return wqe;
    }

    void step_wq() {
        ++head;
        if (head == wqe_cnt) {
            head = 0;
            own_flag = own_flag ^ SMARTNS_SEND_WQE_OWNER_MASK;
        }
    }
};
enum dpu_send_wqe_state {
    dpu_send_wqe_state_posted,
    dpu_send_wqe_state_processing,
    dpu_send_wqe_state_pending,
    dpu_send_wqe_state_done,
    dpu_send_wqe_state_error,
};

struct alignas(64) dpu_send_wqe {
    uint64_t local_addr;
    uint64_t remote_addr;
    uint32_t local_lkey;
    uint32_t byte_count;
    uint32_t imm;

    uint32_t remote_rkey;
    uint32_t opcode;
    uint32_t cur_pos;

    dpu_send_wqe_state state;
    uint32_t first_psn;
    uint32_t last_psn;
    uint32_t cur_pkt_num;
    uint32_t cur_pkt_offset;

    uint8_t is_signal;
};

static_assert(sizeof(dpu_send_wqe) == 64, "dpu_send_wqe size must be 64 bytes");

struct alignas(64) dpu_send_wq {
    struct dpu_context *dpu_ctx;
    void *bf_send_wq_buf;

    uint32_t wqe_size;
    uint32_t wqe_cnt;
    uint32_t wqe_shift;
    uint32_t head;
    uint32_t tail;

    // used for RoCEV2
    uint32_t wqe_index;
    uint32_t psn;
    int opcode;
    int	noack_pkts;


    dpu_send_wqe *get_next_wqe() {
        dpu_send_wqe *wqe = reinterpret_cast<dpu_send_wqe *>(reinterpret_cast<uint8_t *>(bf_send_wq_buf) + (head << wqe_shift));
        return wqe;
    }

    dpu_send_wqe *get_wqe(uint32_t index) {
        dpu_send_wqe *wqe = reinterpret_cast<dpu_send_wqe *>(reinterpret_cast<uint8_t *>(bf_send_wq_buf) + (index << wqe_shift));
        return wqe;
    }

    void step_head() {
        ++head;
        if (head == wqe_cnt) {
            head = 0;
        }
    }

    void step_wqe_index() {
        ++wqe_index;
        if (wqe_index == wqe_cnt) {
            wqe_index = 0;
        }
    }

    void step_tail() {
        ++tail;
        if (tail == wqe_cnt) {
            tail = 0;
        }
    }

    bool is_full() {
        return (tail + 1) % wqe_cnt == head;
    }

    bool is_empty() {
        return head == tail;
    }
};

struct dpu_mr {
    unsigned int host_mkey;
    struct devx_mr *devx_mr;
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
    uint32_t now_total_dma_byte;

    // used for RoCEV2
    uint32_t psn;
    uint32_t ack_psn;
    uint32_t msn;
    int opcode;
    int sent_psn_nak;

    // used for Read/Write
    uint64_t host_va;
    uint64_t offset;
    uint32_t byte_count;
    uint32_t host_rkey;
    uint32_t resid;
    dpu_mr *mr;

    uint8_t own_flag;

    inline smartns_recv_wqe *get_next_wqe() {
        smartns_recv_wqe *wqe = reinterpret_cast<smartns_recv_wqe *>(reinterpret_cast<uint8_t *>(bf_recv_wq_buf) + (head << wqe_shift));
        if (wqe->op_own != own_flag) {
            SMARTNS_WARN("head %u, own_flag %u, but wqe_own %u not match\n", head, own_flag, wqe->op_own);
            exit(1);
        }
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
        now_total_dma_byte = 0;
    }
};

struct alignas(64) dpu_comp_info {
    uint32_t psn;
    int opcode;
    int timeout;
};

struct alignas(64) dpu_qp {
    struct dpu_context *dpu_ctx;
    struct dpu_pd *dpu_pd;
    size_t qp_number;
    size_t remote_qp_number;
    ibv_qp_type qp_type;
    int mtu;
    unsigned int max_send_wr;
    unsigned int max_recv_wr;
    unsigned int max_send_sge;
    unsigned int max_recv_sge;
    unsigned int max_inline_data;

    struct dpu_cq *send_cq;
    struct dpu_cq *recv_cq;

    struct dpu_datapath_send_wq *datapath_send_wq;
    struct dpu_send_wq *send_wq;
    struct dpu_comp_info *comp_info;
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


struct dpu_context {
    int host_pid;
    int host_tgid;

    size_t context_number;
    struct devx_mr *inner_bf_mr;
    struct devx_mr *inner_host_mr;

    std::vector<dpu_datapath_send_wq> datapath_send_wq_list;

    // pdn to struct pd
    phmap::parallel_flat_hash_map<size_t, dpu_pd *>pd_list;
    // cqn to struct cq
    phmap::parallel_flat_hash_map<size_t, dpu_cq *>cq_list;
    // qpn to struct qp
    phmap::parallel_flat_hash_map<size_t, dpu_qp *>qp_list;
    // host mkey to mr
    phmap::parallel_flat_hash_map<unsigned int, dpu_mr *>mr_list;
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
    uint32_t *dma_count_list;
    //plus one when dma packet payload, and reset to 0 when set signal
    uint64_t *payload_count_list;

    ibv_qp *cqe_qp;
    ibv_qp_ex *cqe_qpx;
    mlx5dv_qp_ex *cqe_mqpx;
    uint32_t cqe_count;

    ibv_qp **invalid_qp_list;
    ibv_qp_ex **invalid_qpx_list;
    mlx5dv_qp_ex **invalid_mqpx_list;
    uint32_t *invalid_start_index_list;
    uint32_t *invalid_finish_index_list;

    uint32_t now_use_qp_index;
    uint32_t qp_group_size;

    // this cq will be shared within all DMA QP
    ibv_cq *dma_send_recv_cq;
    // this cq will be shared within all cache invalid QP and cqe
    ibv_cq *invalid_send_recv_cq;

    inline void post_dma_req_without_cq(uint32_t dest_lkey, uint64_t dest_addr,
        uint32_t src_lkey, uint64_t src_addr, size_t length) {

        bool is_signal = dma_count_list[now_use_qp_index] % SMARTNS_DMA_BATCH >= (SMARTNS_DMA_BATCH - 1);

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

    inline void post_send_recv_cqe(dpu_cq *cq) {
        bool is_signal = cqe_count % 16 == 15;

        cqe_qpx->wr_id = cqe_count;
        cqe_qpx->wr_flags = is_signal ? IBV_SEND_SIGNALED : 0;
        size_t cqe_offset = (cq->head & (cq->wqe_cnt - 1)) << cq->wqe_shift;
        cqe_mqpx->wr_memcpy_direct(cqe_mqpx, cq->host_mkey, reinterpret_cast<uint64_t>(cq->host_cq_buf) + cqe_offset, cq->bf_mkey, reinterpret_cast<uint64_t>(cq->bf_cq_buf) + cqe_offset, cq->wqe_size);
        cqe_count++;
    }

    inline uint32_t poll_dma_cq() {
        uint32_t total_finish_dma = 0;
        ibv_wc wc[16];

        uint32_t num_wc = ibv_poll_cq(dma_send_recv_cq, 16, wc);
        for (uint32_t i = 0; i < num_wc; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                SMARTNS_ERROR("dma cq error %d %ld\n", wc[i].status, wc[i].wr_id);
                exit(-1);
            }
            uint64_t wr_id = wc[i].wr_id;
            uint32_t qp_index = wr_id & 0xFFFFFFFF;
            uint32_t payload_count = wr_id >> 32;
            invalid_mqpx_list[qp_index]->wr_invcache_direct_flush(invalid_mqpx_list[qp_index], invalid_finish_index_list[qp_index], payload_count);
            invalid_finish_index_list[qp_index] += payload_count;
            total_finish_dma += payload_count;
        }

        num_wc = ibv_poll_cq(invalid_send_recv_cq, 16, wc);
        for (uint32_t i = 0; i < num_wc; i++) {
            assert(wc[i].status == IBV_WC_SUCCESS);
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

    uint32_t batch_index;
    uint32_t wr_index;

    inline void *get_next_pktheader_addr() {
        return reinterpret_cast<void *>(send_buf_addr + send_offset_handler.offset());
    }

    inline void commit_pkt_with_payload(uint64_t remote_addr, uint32_t rkey, uint32_t header_size, uint32_t payload_size) {
        udp_packet *packet = reinterpret_cast<udp_packet *>(send_offset_handler.offset() + send_buf_addr);
        packet->ip_hdr.total_length = htons(header_size + payload_size - sizeof(ether_hdr));
        packet->udp_hdr.dgram_len = htons(header_size + payload_size - sizeof(ether_hdr) - sizeof(ipv4_hdr));

        send_sge_list[wr_index * num_sges_per_wr].addr = send_offset_handler.offset() + send_buf_addr;
        send_sge_list[wr_index * num_sges_per_wr].length = header_size;

        send_sge_list[wr_index * num_sges_per_wr + 1].addr = remote_addr;
        send_sge_list[wr_index * num_sges_per_wr + 1].length = payload_size;
        send_sge_list[wr_index * num_sges_per_wr + 1].lkey = rkey;

        send_wr[wr_index].num_sge = 2;
        if (wr_index > 0) {
            send_wr[wr_index - 1].next = send_wr + wr_index;
        }
        send_wr[wr_index].next = nullptr;
        send_wr[wr_index].wr_id = send_offset_handler.offset() + send_buf_addr;
        send_wr[wr_index].send_flags = batch_index == SMARTNS_TX_BATCH - 1 ? (IBV_SEND_IP_CSUM | IBV_SEND_SIGNALED) : IBV_SEND_IP_CSUM;

        batch_index = (batch_index + 1) % SMARTNS_TX_BATCH;
        wr_index++;
        send_offset_handler.step();

        if (wr_index == SMARTNS_TX_BATCH) {
            commit_flush();
        }
    }

    inline void commit_pkt_without_payload(uint32_t header_size) {
        udp_packet *packet = reinterpret_cast<udp_packet *>(send_offset_handler.offset() + send_buf_addr);
        packet->ip_hdr.total_length = htons(header_size - sizeof(ether_hdr));
        packet->udp_hdr.dgram_len = htons(header_size - sizeof(ether_hdr) - sizeof(ipv4_hdr));

        send_sge_list[wr_index * num_sges_per_wr].addr = send_offset_handler.offset() + send_buf_addr;
        send_sge_list[wr_index * num_sges_per_wr].length = header_size;

        send_wr[wr_index].num_sge = 1;
        if (wr_index > 0) {
            send_wr[wr_index - 1].next = send_wr + wr_index;
        }
        send_wr[wr_index].next = nullptr;
        send_wr[wr_index].wr_id = send_offset_handler.offset() + send_buf_addr;
        send_wr[wr_index].send_flags = batch_index == SMARTNS_TX_BATCH - 1 ? (IBV_SEND_IP_CSUM | IBV_SEND_SIGNALED) : IBV_SEND_IP_CSUM;

        batch_index = (batch_index + 1) % SMARTNS_TX_BATCH;
        wr_index++;
        send_offset_handler.step();

        if (wr_index == SMARTNS_TX_BATCH) {
            commit_flush();
        }
    }

    inline void commit_flush() {
        if (wr_index == 0) {
            return;
        }
        assert(ibv_post_send(send_qp, send_wr, &send_bad_wr) == 0);
        wr_index = 0;
    }
    inline void poll_tx_cq() {
        ibv_wc wc[16];
        int recv = ibv_poll_cq(send_cq, 16, wc);
        for (int i = 0;i < recv;i++) {
            if (wc[i].status != IBV_WC_SUCCESS || wc[i].opcode != IBV_WC_SEND) {
                SMARTNS_ERROR("tx cq error status %d opcode %d\n", wc[i].status, wc[i].opcode);
                exit(-1);
            }
            send_comp_offset_handler.step(SMARTNS_TX_BATCH);
        }
    }
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

    // don't need use parallel hash map
    phmap::flat_hash_map<uint64_t, dpu_qp *>local_qpn_to_qp_list;

    phmap::flat_hash_set<dpu_qp *>active_qp_list;
    phmap::parallel_flat_hash_set<dpu_datapath_send_wq *>active_datapath_send_wq_list;

    void loop_datapath_send_wq();

    size_t handle_send();

    size_t handle_recv();

    void dma_send_payload_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size);

    void dma_write_payload_to_host(dpu_qp *qp, void *paylod_buf, size_t payload_size);

    void dma_send_cq_to_host(dpu_qp *qp);

    void dma_recv_cq_to_host(dpu_qp *qp);
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

    phmap::parallel_flat_hash_map<size_t, dpu_context *>context_list;

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