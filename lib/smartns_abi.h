#pragma once

#include <asm-generic/ioctl.h>

#define SMARTNS_IOCTL 0x12

#define SMARTNS_CONTEXT_ALLOC_SIZE (8*1024*1024)

static __attribute__((unused)) const char *smartnsinode = "/dev/smartns";

static __attribute__((unused)) uint8_t vhca_access_key[32] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1
};
#define SMARTNS_SEND_WQE_OWNER_MASK 1

#define SMARTNS_RECV_WQE_OWNER_MASK 1

#define SMARTNS_CQE_OWNER_MASK 1

struct __attribute__((packed)) smartns_send_wqe {
    uint64_t qpn;
    uint32_t opcode;
    uint32_t imm;

    uint64_t local_addr;
    uint32_t local_lkey;
    uint32_t byte_count;

    uint64_t remote_addr;
    uint32_t remote_rkey;
    uint32_t reserved1;

    uint32_t cur_pos;
    uint8_t is_signal;
    uint8_t op_own;

    uint8_t reserved2[10];
};

struct __attribute__((packed)) smartns_recv_wqe {
    uint64_t addr;
    uint32_t lkey;
    uint32_t byte_count : 24;
    uint8_t op_own : 8;
};

struct __attribute__((packed)) smartns_cqe {
    uint64_t qpn;
    uint32_t byte_count;
    uint16_t wqe_counter;
    uint16_t mlx5_opcode;
    uint8_t cq_opcode;
    uint8_t op_own;
    uint8_t reserved[46];
};

struct __attribute__((packed)) smartns_cq_doorbell {
    size_t consumer_index;
    char padding[56];
};

struct SMARTNS_KERNEL_COMMON_PARAMS {
    int pid;
    int tgid;
    unsigned int cmd;
    // fill by bf, 1 is success, 0 is fail
    unsigned int success;
};

struct SMARTNS_OPEN_DEVICE_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned short int host_vhca_id;
    unsigned int host_mkey;
    unsigned long int host_size;
    void *host_addr;

    // response
    unsigned short int bf_vhca_id;
    unsigned int bf_mkey;
    unsigned long int bf_size;
    void *bf_addr;

    // bf always use begin of bf_addr as send_wq, from [bf_addr, bf_addr + send_wq_number * send_wq_capacity * sizeof(smartns_send_wqe))]
    unsigned int send_wq_number;
    unsigned int send_wq_capacity;

    unsigned long int context_number;
};

struct SMARTNS_CLOSE_DEVICE_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
};

struct SMARTNS_ALLOC_PD_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;

    // response
    unsigned long int pd_number;
};

struct SMARTNS_DEALLOC_PD_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
};


struct SMARTNS_REG_MR_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
    unsigned short int host_vhca_id;
    unsigned int host_mkey;
    unsigned long int host_size;
    void *host_addr;

    // response
    unsigned int bf_mkey;
};

struct SMARTNS_DESTROY_MR_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
    unsigned int host_mkey;
};


struct SMARTNS_CREATE_CQ_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned int max_num;
    void *host_cq_buf;
    void *host_cq_doorbell;
    void *bf_cq_buf;
    void *bf_cq_doorbell;

    // response
    unsigned long int cq_number;
};

struct SMARTNS_DESTROY_CQ_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int cq_number;
};

struct SMARTNS_CREATE_QP_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;

    unsigned long int datapath_send_wq_id;
    unsigned long int recv_wq_size;
    void *host_recv_wq_addr;
    void *bf_recv_wq_addr;

    unsigned long int send_cq_number;
    unsigned long int recv_cq_number;

    unsigned int max_send_wr;
    unsigned int max_recv_wr;
    unsigned int max_send_sge;
    unsigned int max_recv_sge;
    unsigned int max_inline_data;

    int qp_type;
    // response
    unsigned long int qp_number;
};

struct SMARTNS_MODIFY_QP_PARAMS {

};

struct SMARTNS_DESTROY_QP_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
    unsigned long int qp_number;
};

#define SMARTNS_IOC_OPEN_DEVICE _IOWR(SMARTNS_IOCTL, 1, struct SMARTNS_OPEN_DEVICE_PARAMS)

#define SMARTNS_IOC_ALLOC_PD _IOWR(SMARTNS_IOCTL, 2, struct SMARTNS_ALLOC_PD_PARAMS)

#define SMARTNS_IOC_REG_MR _IOWR(SMARTNS_IOCTL, 3, struct SMARTNS_REG_MR_PARAMS)

#define SMARTNS_IOC_CREATE_CQ _IOWR(SMARTNS_IOCTL, 4, struct SMARTNS_CREATE_CQ_PARAMS)

#define SMARTNS_IOC_CREATE_QP _IOWR(SMARTNS_IOCTL, 5, struct SMARTNS_CREATE_QP_PARAMS)

#define SMARTNS_IOC_MODIFY_QP _IOWR(SMARTNS_IOCTL, 6, struct SMARTNS_MODIFY_QP_PARAMS)

#define SMARTNS_IOC_DESTROY_QP _IOWR(SMARTNS_IOCTL, 7, struct SMARTNS_DESTROY_QP_PARAMS)

#define SMARTNS_IOC_DESTROY_CQ _IOWR(SMARTNS_IOCTL, 8, struct SMARTNS_DESTROY_CQ_PARAMS)

#define SMARTNS_IOC_DESTROY_MR _IOWR(SMARTNS_IOCTL, 9, struct SMARTNS_DESTROY_MR_PARAMS)

#define SMARTNS_IOC_DEALLOC_PD _IOWR(SMARTNS_IOCTL, 10, struct SMARTNS_DEALLOC_PD_PARAMS)

#define SMARTNS_IOC_CLOSE_DEVICE _IOWR(SMARTNS_IOCTL, 11, struct SMARTNS_CLOSE_DEVICE_PARAMS)
