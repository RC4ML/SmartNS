#include "smartns_dv.h"
#include "rdma_cm/libr.h"
#include "gflags_common.h"
#include "hdr_histogram.h"

std::atomic<bool> stop_flag = false;

void ctrl_c_handler(int) { stop_flag = true; }


int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    assert(setenv("MLX5_TOTAL_UUARS", "33", 0) == 0);
    assert(setenv("MLX5_NUM_LOW_LAT_UUARS", "32", 0) == 0);

    struct ibv_device *ib_dev = ctx_find_dev("mlx5_0");

    struct ibv_context *context = smartns_open_device(ib_dev);
    assert(context);

    struct ibv_pd *pd = smartns_alloc_pd(context);
    assert(pd);

    size_t alloc_size = 8 * 1024 * 1024;
    void *addr = aligned_alloc(alloc_size, PAGE_SIZE);
    assert(addr);
    struct ibv_mr *mr = smartns_reg_mr(pd, addr, alloc_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    assert(mr);

    struct ibv_cq *send_cq = smartns_create_cq(context, 512, nullptr, nullptr, 0);
    struct ibv_cq *recv_cq = smartns_create_cq(context, 512, nullptr, nullptr, 0);

    struct ibv_qp_init_attr qp_init_attr;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.send_cq = send_cq;
    qp_init_attr.recv_cq = recv_cq;
    qp_init_attr.sq_sig_all = 0;
    qp_init_attr.cap.max_send_wr = 512;
    qp_init_attr.cap.max_recv_wr = 512;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;

    struct ibv_qp *qp = smartns_create_qp(pd, &qp_init_attr);
    assert(qp);


    struct ibv_recv_wr recv_wr;
    struct ibv_sge recv_sge;
    recv_wr.wr_id = 1;
    recv_wr.next = nullptr;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    struct ibv_send_wr send_wr;
    struct ibv_sge send_sge;
    send_wr.wr_id = 1;
    send_wr.next = nullptr;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;


    struct ibv_wc *wc_send = NULL;
    struct ibv_wc *wc_recv = NULL;
    ALLOCATE(wc_send, struct ibv_wc, CTX_POLL_BATCH);
    ALLOCATE(wc_recv, struct ibv_wc, CTX_POLL_BATCH);

    size_t send_size = alloc_size / 2;
    offset_handler send(send_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler send_comp(send_size / FLAGS_payload_size, FLAGS_payload_size, 0);
    offset_handler recv(send_size / FLAGS_payload_size, FLAGS_payload_size, send_size);
    offset_handler recv_comp(send_size / FLAGS_payload_size, FLAGS_payload_size, send_size);

    size_t recv_index = 0;
    for (recv_index = 0;recv_index < 512;recv_index++) {
        recv_sge.addr = reinterpret_cast<uint64_t>(addr) + recv.offset();
        recv_sge.length = FLAGS_payload_size;
        recv_sge.lkey = mr->lkey;
        recv_wr.wr_id = recv.index();
        struct ibv_recv_wr *bad_wr;
        assert(smartns_post_recv(qp, &recv_wr, &bad_wr) == 0);
        recv.step();
    }

    sleep(1);

    Histogram latency_histogram(1000, 3000000, 10.0);
    size_t tsc_value = 0;
    size_t index = 0;
    while (!stop_flag) {
        if (FLAGS_is_server) {
            struct ibv_wc wc;
            int ret = smartns_poll_cq(recv_cq, 1, &wc);
            if (ret == 1) {
                assert(wc.status == IBV_WC_SUCCESS);
                if (wc.byte_len != FLAGS_payload_size) {
                    printf("[%ld] byte_len %u error\n", index, wc.byte_len);
                }
                assert(wc.wr_id == recv_comp.index());
                // printf("[%ld] recv status %d byte_len %d\n", index, wc.status, wc.byte_len);
                recv_sge.addr = reinterpret_cast<uint64_t>(addr) + recv.offset();
                recv_sge.length = FLAGS_payload_size;
                recv_sge.lkey = mr->lkey;
                recv_wr.wr_id = recv.index();

                struct ibv_recv_wr *bad_wr;
                assert(smartns_post_recv(qp, &recv_wr, &bad_wr) == 0);
                recv.step();
                recv_comp.step();
                index++;
            }
        } else {
            send_sge.addr = reinterpret_cast<uint64_t>(addr) + send.offset();
            send_sge.length = FLAGS_payload_size;
            send_sge.lkey = mr->lkey;
            send_wr.wr_id = send.index();
            struct ibv_send_wr *bad_wr;
            assert(smartns_post_send(qp, &send_wr, &bad_wr) == 0);
            send.step();
            tsc_value = get_tsc();
            while (!stop_flag) {
                struct ibv_wc wc;
                int ret = smartns_poll_cq(send_cq, 1, &wc);
                if (ret == 1) {
                    assert(wc.status == IBV_WC_SUCCESS);
                    latency_histogram.record(get_tsc() - tsc_value);
                    if (wc.byte_len != FLAGS_payload_size) {
                        printf("[%ld] byte_len %u error\n", index, wc.byte_len);
                    }
                    assert(wc.wr_id == send_comp.index());
                    // printf("[%ld]send comp status %d byte_len %d\n", index, wc.status, wc.byte_len);
                    send_comp.step();
                    index++;
                    break;
                }
            }
        }
    }

    latency_histogram.print(stdout, 5);

    assert(smartns_destroy_qp(qp) == 0);

    assert(smartns_destroy_cq(send_cq) == 0);

    assert(smartns_destroy_cq(recv_cq) == 0);

    assert(smartns_dereg_mr(mr) == 0);

    assert(smartns_dealloc_pd(pd) == 0);

    assert(smartns_close_device(context) == 0);
}