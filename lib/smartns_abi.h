#pragma once

#include <asm-generic/ioctl.h>
const char *smartnsinode = "/dev/smartns";

#define SMARTNS_IOCTL 0x12

struct SMARTNS_KERNEL_COMMON_PARAMS {
    int pid;
    int tgid;
    unsigned int cmd;
    // fill by bf, 1 is success, 0 is fail
    unsigned int success;
};

struct SMARTNS_IOC_OPEN_DEVICE_PARAMS {
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

struct SMARTNS_IOC_CLOSE_DEVICE_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
};

struct SMARTNS_IOC_ALLOC_PD_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;

    // response
    unsigned long int pd_number;
};

struct SMARTNS_DESTROY_PD_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
};


struct SMARTNS_IOC_REG_MR_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned short int host_vhca_id;
    unsigned int host_mkey;
    unsigned long int host_size;
    void *host_addr;
};

struct SMARTNS_DESTROY_MR_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;
    unsigned int lkey;
};


struct SMARTNS_IOC_CREATE_CQ_PARAMS {
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

struct SMARTNS_IOC_CREATE_QP_PARAMS {
    struct SMARTNS_KERNEL_COMMON_PARAMS common_params;
    unsigned long int context_number;
    unsigned long int pd_number;

    unsigned long int send_wq_id;
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






#define SMARTNS_IOC_OPEN_DEVICE _IOWR(SMARTNS_IOCTL, 1, struct SMARTNS_IOC_OPEN_DEVICE_PARAMS)

#define SMARTNS_IOC_ALLOC_PD _IOWR(SMARTNS_IOCTL, 2, struct SMARTNS_IOC_ALLOC_PD_PARAMS)

#define SMARTNS_IOC_REG_MR _IOWR(SMARTNS_IOCTL, 3, struct SMARTNS_IOC_REG_MR_PARAMS)

#define SMARTNS_IOC_CREATE_CQ _IOWR(SMARTNS_IOCTL, 4, struct SMARTNS_IOC_CREATE_CQ_PARAMS)

#define SMARTNS_IOC_CREATE_QP _IOWR(SMARTNS_IOCTL, 5, struct SMARTNS_IOC_CREATE_QP_PARAMS)

#define SMARTNS_IOC_MODIFY_QP _IOWR(SMARTNS_IOCTL, 6, struct SMARTNS_MODIFY_QP_PARAMS)

#define SMARTNS_IOC_DESTROY_QP _IOWR(SMARTNS_IOCTL, 7, struct SMARTNS_DESTROY_QP_PARAMS)

#define SMARTNS_IOC_DESTROY_CQ _IOWR(SMARTNS_IOCTL, 8, struct SMARTNS_DESTROY_CQ_PARAMS)

#define SMARTNS_IOC_DESTROY_MR _IOWR(SMARTNS_IOCTL, 9, struct SMARTNS_DESTROY_MR_PARAMS)

#define SMARTNS_IOC_DESTROY_PD _IOWR(SMARTNS_IOCTL, 10, struct SMARTNS_DESTROY_PD_PARAMS)

#define SMARTNS_IOC_CLOSE_DEVICE _IOWR(SMARTNS_IOCTL, 11, struct SMARTNS_IOC_CLOSE_DEVICE_PARAMS)
