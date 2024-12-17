#include "smartns_dv.h"
#include "rdma_cm/libr.h"
int main() {
    struct ibv_device *ib_dev = ctx_find_dev("mlx5_0");

    struct ibv_context *context = smartns_open_device(ib_dev);
    assert(context);

    struct ibv_pd *pd = smartns_alloc_pd(context);
    assert(pd);

    void *addr = aligned_alloc(8 * 1024 * 1024, PAGE_SIZE);
    assert(addr);
    struct ibv_mr *mr = smartns_reg_mr(pd, addr, 8 * 1024 * 1024, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    assert(mr);

    struct ibv_cq *send_cq = smartns_create_cq(context, 128, nullptr, nullptr, 0);
    struct ibv_cq *recv_cq = smartns_create_cq(context, 128, nullptr, nullptr, 0);

    struct ibv_qp_init_attr qp_init_attr;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.send_cq = send_cq;
    qp_init_attr.recv_cq = recv_cq;
    qp_init_attr.sq_sig_all = 0;
    qp_init_attr.cap.max_send_wr = 256;
    qp_init_attr.cap.max_recv_wr = 256;
    qp_init_attr.cap.max_send_wr = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;

    struct ibv_qp *qp = smartns_create_qp(pd, &qp_init_attr);
    assert(qp);

    for (size_t i = 0;i < 256;i++) {
        struct ibv_recv_wr wr;
        struct ibv_sge sge;
        wr.wr_id = 1;
        wr.next = nullptr;
        wr.sg_list = &sge;
        wr.num_sge = 1;

        sge.addr = reinterpret_cast<uint64_t>(addr) + i * 4096;
        sge.length = 4096;
        sge.lkey = mr->lkey;

        struct ibv_recv_wr *bad_wr;
        assert(smartns_post_recv(qp, &wr, &bad_wr) == 0);
    }

    sleep(100);

    assert(smartns_destroy_qp(qp) == 0);

    assert(smartns_destroy_cq(send_cq) == 0);

    assert(smartns_destroy_cq(recv_cq) == 0);

    assert(smartns_dereg_mr(mr) == 0);

    assert(smartns_dealloc_pd(pd) == 0);

    assert(smartns_close_device(context) == 0);
}