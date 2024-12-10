#pragma once

#include "common.hpp"
#include "devx.h"

struct devx_mr {
    // ibv_mr
    struct ibv_context *context;
    struct ibv_pd *pd;
    void *addr;
    size_t length;
    uint32_t handle;
    uint32_t lkey;
    uint32_t rkey;
    struct ibv_mr *ib_mr;
    struct mlx5dv_devx_umem *umem;
    struct mlx5dv_devx_obj *mkey;
    uint16_t vhca_id;
};

struct vhca_resource {
    ibv_pd *pd;
    devx_mr *mr;

    // exchanged
    uint16_t vhca_id;
    void *addr;
    uint64_t size;
    uint32_t mkey;
};

/*
 * Query pd number of ibv_pd
 *
 * @param pd        the ibv pd
 * @param pdn       pd number
 * @return          return 0 when succeed; return -1 when failed.
 */
int devx_query_ibv_pd_number(struct ibv_pd *pd, uint32_t *pdn);

/*
 * Register a memory region using umem
 *
 * @param pd        the ibv pd
 * @param addr      address of memory region to register
 * @param size      size of memory region to register
 * @param access    access flags, same with ib_reg_mr
 * @return          return pointer of mr when succeed; return NULL when failed.
 */
struct devx_mr *devx_reg_mr(struct ibv_pd *pd, void *addr, size_t size, uint32_t access);

/*
 * Register a memory region using ibv_reg_mr
 *
 * @param pd        the ibv pd
 * @param addr      address of memory region to register
 * @param size      size of memory region to register
 * @param access    access flags, same with ib_reg_mr
 * @return          return pointer of mr when succeed; return NULL when failed.
 */
struct devx_mr *devx_ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t size, uint32_t access);

/*
 * Deregister a memory region
 *
 * @param mr        pointer of memory region
 * @return          return 0 when succeed; return -1 when failed.
 */
int devx_dereg_mr(struct devx_mr *mr);

/*
 * Query mkey of a memory region
 *
 * @param mr        pointer of memory region
 * @return          mkey
 */
uint32_t devx_mr_query_mkey(struct devx_mr *mr);

/*
 * Allow other VHCA access to a memory region
 *
 * @param mr          pointer of memory region
 * @param access_key  pointer of access key
 * @param len         length of access key, MUST be 32
 * @return            return 0 when succeed; return -1 when failed.
 */
int devx_mr_allow_other_vhca_access(struct devx_mr *mr, void *access_key, size_t len);

/*
 * Create a crossing memory region
 *
 * @param pd              the ibv pd
 * @param addr            address of memory region
 * @param size            size of memory region
 * @param vhca_id         vhca id to access
 * @param other_mkey      mkey to access
 * @param access_key      pointer of access key
 * @param access_key_len  length of access key, MUST be 32
 * @return                return pointer of mr when succeed; return NULL when failed.
 */
struct devx_mr *devx_create_crossing_mr(struct ibv_pd *pd, void *addr, size_t size, uint16_t vhca_id, uint32_t other_mkey, void *access_key, size_t access_key_len);

/*
 * Create a crossing vhca memory region
 *
 * @param pd                  the ibv pd
 * @param introspection_mkey  introspection mkey to access
 * @param vhca_id             vhca id to access
 * @return                    return pointer of mr when succeed; return NULL when failed.
 */
struct devx_mr *devx_create_crossing_vhca_mr(struct ibv_pd *pd, uint32_t introspection_mkey, uint16_t vhca_id, uint32_t access_flags);

/*
 * Create a indirect memory region
 *
 * @param pd              the ibv pd
 * @param addr            address of memory region
 * @param size            size of memory region
 * @param access_flags    access flags, same with ib_reg_mr
 * @param klm_array       array of KLMs
 * @param klm_num         number of KLMs
 * @return                return pointer of mr when succeed; return NULL when failed.
 */
struct devx_mr *devx_create_indirect_mr(struct ibv_pd *pd, uint64_t addr, uint64_t len, uint32_t access_flags, struct devx_klm *klm_array, int klm_num);
