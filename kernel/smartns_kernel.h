#ifndef __SMARTNS_H__
#define __SMARTNS_H__

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/ib_cache.h>

#define SMARTNS_BF_TCP_PORT 6666


#define SMARTNS_TX_DEPTH 128
#define SMARTNS_RX_DEPTH 128
#define SMARTNS_NUM_WRS 1   // = batch size
#define SMARTNS_NUM_SGES_PER_WR 1 // = sge per wr
#define SMARTNS_MSG_SIZE 512
#define SMARTNS_RDMA_GID_INDEX 3
#define SMARTNS_MIN_RNR_TIMER		(12)
#define SMARTNS_DEF_QP_TIME   (14)
#define SMARTNS_CQ_POLL_BATCH 16

#define MODULE_NAME "smartns"
#define  DEVICE_NAME "smartns"

extern unsigned char SMARTNS_BF_IP_ADDR[5];

extern struct ib_device *global_device;

extern struct ib_client smartns_ib_client;

extern struct socket *global_tcp_socket;

extern unsigned int current_mtu;

struct offset_handler {
    int max_num;
    int step_size;
    int buf_offset;
    size_t cur;
};

__attribute__((unused)) static void offset_handler_step(struct offset_handler *handler) {
    handler->cur += 1;
}

__attribute__((unused)) static size_t offset_handler_offset(struct offset_handler *handler) {
    return (handler->cur % handler->max_num) * handler->step_size + handler->buf_offset;
}

struct smartns_qp_handler {
    // used for rdma
    struct ib_pd *pd;
    struct ib_mr *mr;
    struct ib_qp *qp;
    struct ib_cq *send_cq;
    struct ib_cq *recv_cq;
    struct ib_sge *send_sge_list;
    struct ib_sge *recv_sge_list;
    struct ib_send_wr *send_wr;
    struct ib_recv_wr *recv_wr;
    struct ib_wc *send_wc;
    struct ib_wc *recv_wc;

    size_t original_buf;
    struct scatterlist *sg;
    unsigned int sg_offset;

    size_t local_buf; // vmalloc address, can direct load/store
    size_t local_dma_buf; // dma address created by ib_dma_map_sg. 尽管连续的虚拟地址对应的是离散的物理地址，但是RDMA还是需要使用第0个sg的dma地址+offset作为rdma wr的地址
    // https://elixir.bootlin.com/linux/v5.15.102/source/drivers/infiniband/core/verbs.c#L2682

    int num_wrs;
    int num_sges_per_wr;
    int num_sges;
    int tx_depth;
    int rx_depth;

    struct offset_handler send_offset_handler;
    struct offset_handler recv_offset_handler;
};

struct smartns_info {
    struct list_head list;
    struct mutex lock;
    pid_t pid;
    pid_t tgid;
};
typedef struct smartns_info smartns_info_t;

struct PingPongInfo {
    int lid;
    int qpn;
    int psn;
    unsigned rkey;
    unsigned long long vaddr;
    unsigned char	raw_gid[16];
    unsigned char mac[6];
    int gid_index;
    int out_reads;
    int mtu;
};

int smartns_create_qp_and_send_to_bf(struct smartns_qp_handler *now_info);

void smartns_send_reg_mr(struct smartns_qp_handler *info);

void smartns_init_wr_base_send_recv(struct smartns_qp_handler *info);

int smartns_init_qp(struct smartns_qp_handler *now_info, struct PingPongInfo *pingpong_info);

int smartns_free_qp(struct smartns_qp_handler *now_info);

int tcp_client_send(struct socket *sock, const char *buf, const size_t length, unsigned long flags);

int tcp_client_receive(struct socket *sock, char *str, unsigned long flags);

int tcp_connect_to_bf(void);

struct SMARTNS_TEST_PARAMS {
    size_t batch_size;
};
#define SMARTNS_IOC_TEST _IOWR(SMARTNS_IOCTL, 1, struct SMARTNS_TEST_PARAMS)

// ----------

#endif 