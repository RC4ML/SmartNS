/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#ifndef PRIV_DOCA_MLX5_PRM_MANUAL_H_
#define PRIV_DOCA_MLX5_PRM_MANUAL_H_

#include <stdint.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

#define PRIV_DOCA_MLX5_GENERAL_OBJ_TYPES_CAP_CHANNEL_SERVICE (1ULL << MLX5_GEN_OBJ_TYPE_CHANNEL_SERVICE)

#define PRIV_DOCA_MLX5_GENERAL_OBJ_TYPES_CAP_CHANNEL_CONNECTION (1ULL << MLX5_GEN_OBJ_TYPE_CHANNEL_CONNECTION)

    enum {
        MLX5_GEN_OBJ_TYPE_MKEY = 0xff01,
    };

    /*
     * Need to define this since this enumeration value has 64 bits and
     * A-ME doesn't know how to handle this
     */
#define PRIV_DOCA_MLX5_HCA_CAPS_2_ALLOWED_OBJECT_FOR_OTHER_VHCA_ACCESS_DPA_THREAD (1ULL << 0x2b)

    enum priv_doca_mlx5_qpc_opt_mask_32 {
        PRIV_DOCA_MLX5_QPC_OPT_MASK_32_INIT2INIT_MMO = 1 << 3,
    };

    struct mlx5_ifc_qpc_extension_and_pas_list_bits {
        struct mlx5_ifc_qp_context_extension_bits qpc_data_extension;
        uint8_t pas[0][0x40];
    };

    struct mlx5_ifc_create_alias_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
        struct mlx5_ifc_alias_context_bits alias_ctx;
    };

    struct mlx5_ifc_create_alias_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits hdr;
    };

    struct mlx5_ifc_create_channel_service_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
        struct mlx5_ifc_channel_service_obj_bits ch_svc_obj;
    };

    struct mlx5_ifc_create_channel_service_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits hdr;
    };

    struct mlx5_ifc_create_channel_connection_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
        struct mlx5_ifc_channel_connection_obj_bits ch_conn_obj;
    };

    struct mlx5_ifc_create_channel_connection_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits hdr;
    };

    struct mlx5_ifc_create_encryption_key_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
        struct mlx5_ifc_encryption_key_obj_bits encryption_key_obj;
    };

    struct mlx5_ifc_modify_encryption_key_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
        struct mlx5_ifc_encryption_key_obj_bits encryption_key_obj;
    };

    /* Common fields for all emulation objects*/

    struct mlx5_ifc_emulation_obj_common_bits {
        uint8_t modify_field_select[0x40];

        uint8_t reserved_at_40[0x20];

        uint8_t enabled[0x1];
        uint8_t reserved_at_61[0x4];
        uint8_t pci_hotplug_state[0x3];
        uint8_t reserved_at_68[0x18];

        uint8_t reserved_at_80[0x780];
    };

    union mlx5_ifc_emulation_obj_bits {
        struct mlx5_ifc_generic_emulation_bits generic_emulation_object;
        struct mlx5_ifc_emulation_obj_common_bits emulation_object_common;
        struct mlx5_ifc_virtio_fs_device_emulation_object_bits virtio_fs_device_emulation_object;
        uint8_t reserved_at_0[0x800];
    };

    struct mlx5_ifc_create_emulation_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;

        union mlx5_ifc_emulation_obj_bits obj_context;
    };

    struct mlx5_ifc_create_emulation_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
    };

    struct mlx5_ifc_modify_emulation_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;

        union mlx5_ifc_emulation_obj_bits obj_context;
    };

    struct mlx5_ifc_modify_emulation_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
    };

    struct mlx5_ifc_query_emulation_obj_in_bits {
        struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;
    };

    struct mlx5_ifc_query_emulation_obj_out_bits {
        struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;

        union mlx5_ifc_emulation_obj_bits obj_context;
    };

    enum {
        PRIV_DOCA_MLX5_SEND_WQE_BB_SHIFT = 6,
        PRIV_DOCA_MLX5_SEND_WQE_BB = 1 << PRIV_DOCA_MLX5_SEND_WQE_BB_SHIFT,
    };

    struct priv_doca_mlx5_wqe_ctrl_seg {
        __be32 opmod_idx_opcode;
        __be32 qpn_ds;
        union {
            struct {
                uint8_t signature;
                uint8_t rsvd[2];
                uint8_t fm_ce_se;
            };
            struct {
                __be32 flags;
            };
        };

        __be32 imm;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_ctrl_seg) == 16, "struct priv_doca_mlx5_wqe_ctrl_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_umr_ctrl_seg {
        uint8_t flags;
        uint8_t rsvd0[3];
        __be16 klm_octowords;
        union {
            __be16 translation_offset;
            __be16 bsf_octowords;
        };
        __be64 mkey_mask;
        uint8_t rsvd1[32];
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_umr_ctrl_seg) == 48,
        "struct priv_doca_mlx5_wqe_umr_ctrl_seg is not 48 bytes.");

    struct priv_doca_mlx5_wqe_umr_klm_seg {
        /* up to 2GB */
        __be32 byte_count;
        __be32 mkey;
        __be64 address;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_umr_klm_seg) == 16,
        "struct priv_doca_mlx5_wqe_umr_klm_seg is not 16 bytes.");

    union priv_doca_mlx5_wqe_umr_inline_seg {
        struct priv_doca_mlx5_wqe_umr_klm_seg klm;
    };
    static_assert(sizeof(union priv_doca_mlx5_wqe_umr_inline_seg) == 16,
        "union priv_doca_mlx5_wqe_umr_inline_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_mkey_context_seg {
        uint8_t free;
        uint8_t reserved1;
        uint8_t access_flags;
        uint8_t sf;
        __be32 qpn_mkey;
        __be32 reserved2;
        __be32 flags_pd;
        __be64 start_addr;
        __be64 len;
        __be32 bsf_octword_size;
        __be32 reserved3[4];
        __be32 translations_octword_size;
        uint8_t reserved4[3];
        uint8_t log_page_size;
        __be32 reserved;
        union priv_doca_mlx5_wqe_umr_inline_seg inseg[0];
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_mkey_context_seg) == 64,
        "struct priv_doca_mlx5_wqe_mkey_context_seg is not 64 bytes.");

    struct priv_doca_mlx5_wqe_data_seg {
        __be32 byte_count;
        __be32 lkey;
        __be64 addr;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_data_seg) == 16, "struct priv_doca_mlx5_wqe_data_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_eth_seg {
        union {
            struct {
                __be32 swp_offs;
                uint8_t cs_flags;
                uint8_t swp_flags;
                __be16 mss;
                __be32 metadata;
                __be16 inline_hdr_sz;
                union {
                    __be16 inline_data;
                    __be16 vlan_tag;
                };
            };
            struct {
                __be32 offsets;
                __be32 flags;
                __be32 flow_metadata;
                __be32 inline_hdr;
            };
        };
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_eth_seg) == 16, "struct priv_doca_mlx5_wqe_eth_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_wod_seg {
        __be32 op_inv;
        __be32 mkey;
        __be64 va_63_3_fail_act;
        __be64 data;
        __be64 dmask;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_wod_seg) == 32, "struct priv_doca_mlx5_wqe_wod_seg is not 32 bytes.");

    struct priv_doca_mlx5_wqe_remote_address_seg {
        __be64 remote_virtual_address;
        __be32 rkey;
        __be32 reserved;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_remote_address_seg) == 16,
        "struct priv_doca_mlx5_wqe_remote_address_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_atomic_seg {
        __be64 swap_or_add_data;
        __be64 compare_data;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_atomic_seg) == 16,
        "struct priv_doca_mlx5_wqe_atomic_seg is not 16 bytes.");

    enum {
        PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_ON_CQE_ERROR = 0x0,
        PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_ON_FIRST_CQE_ERROR = 0x1,
        PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_ALWAYS = 0x2,
        PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_AND_EQE = 0x3,
    };

    enum {
        PRIV_DOCA_MLX5_WQE_CTRL_FM_NO_FENCE = 0x0,
        PRIV_DOCA_MLX5_WQE_CTRL_FM_INITIATOR_SMALL_FENCE = 0x1,
        PRIV_DOCA_MLX5_WQE_CTRL_FM_FENCE = 0x2,
        PRIV_DOCA_MLX5_WQE_CTRL_FM_STRONG_ORDERING = 0x3,
        PRIV_DOCA_MLX5_WQE_CTRL_FM_FENCE_AND_INITIATOR_SMALL_FENCE = 0x4,
    };

    /* The completion mode offset in the WQE control segment line 2. */
#define MLX5_COMP_MODE_OFFSET 2

    enum {
        PRIV_DOCA_MLX5_WQE_CTRL_CQ_UPDATE = PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_ALWAYS << MLX5_COMP_MODE_OFFSET,
        PRIV_DOCA_MLX5_WQE_CTRL_CQ_ERROR_UPDATE = PRIV_DOCA_MLX5_WQE_CTRL_CE_CQE_ON_CQE_ERROR << MLX5_COMP_MODE_OFFSET,
        PRIV_DOCA_MLX5_WQE_CTRL_SOLICITED = 1 << 1,
        PRIV_DOCA_MLX5_WQE_CTRL_FENCE = PRIV_DOCA_MLX5_WQE_CTRL_FM_FENCE_AND_INITIATOR_SMALL_FENCE << 5,
        PRIV_DOCA_MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE = PRIV_DOCA_MLX5_WQE_CTRL_FM_INITIATOR_SMALL_FENCE << 5,
        PRIV_DOCA_MLX5_WQE_CTRL_STRONG_ORDERING = PRIV_DOCA_MLX5_WQE_CTRL_FM_STRONG_ORDERING << 5,
    };

    enum {
        PRIV_DOCA_MLX5_RCV_DBR = 0,
        PRIV_DOCA_MLX5_SND_DBR = 1,
    };

    enum priv_doca_mlx5_opcode_type {
        PRIV_DOCA_MLX5_OPCODE_NOP = 0x00,
        PRIV_DOCA_MLX5_OPCODE_SEND_INVAL = 0x01,
        PRIV_DOCA_MLX5_OPCODE_RDMA_WRITE = 0x08,
        PRIV_DOCA_MLX5_OPCODE_RDMA_WRITE_IMM = 0x09,
        PRIV_DOCA_MLX5_OPCODE_SEND = 0x0a,
        PRIV_DOCA_MLX5_OPCODE_SEND_IMM = 0x0b,
        PRIV_DOCA_MLX5_OPCODE_LSO = 0x0e,
        PRIV_DOCA_MLX5_OPCODE_WAIT = 0x0f,
        PRIV_DOCA_MLX5_OPCODE_RDMA_READ = 0x10,
        PRIV_DOCA_MLX5_OPCODE_ATOMIC_CS = 0x11,
        PRIV_DOCA_MLX5_OPCODE_ATOMIC_FA = 0x12,
        PRIV_DOCA_MLX5_OPCODE_ATOMIC_MASK_CS = 0x14,
        PRIV_DOCA_MLX5_OPCODE_ATOMIC_MASK_FA = 0x15,
        PRIV_DOCA_MLX5_OPCODE_BIND_MW = 0x18,
        PRIV_DOCA_MLX5_OPCODE_FMR = 0x19,
        PRIV_DOCA_MLX5_OPCODE_LOCAL_INVAL = 0x1b,
        PRIV_DOCA_MLX5_OPCODE_CONFIG_CMD = 0x1f,
        PRIV_DOCA_MLX5_OPCODE_DUMP = 0x23,
        PRIV_DOCA_MLX5_OPCODE_UMR = 0x25,
        PRIV_DOCA_MLX5_OPCODE_ENHANCED_MPSW = 0x29,
        PRIV_DOCA_MLX5_OPCODE_MMO = 0x2f,
        PRIV_DOCA_MLX5_OPCODE_LOCAL_MMO = 0x32,
        PRIV_DOCA_MLX5_OPCODE_INVALID = 0xff
    };

    enum {
        PRIV_DOCA_MLX5_OPC_MOD_MMO_SHA = 0x0,
        PRIV_DOCA_MLX5_OPC_MOD_LOCAL_MMO_TRANSPOSE = 0x0,
        PRIV_DOCA_MLX5_OPC_MOD_LOCAL_MMO_LOCAL_DMA = 0x1,
        PRIV_DOCA_MLX5_OPC_MOD_WAIT_ON_DATA = 0x1,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_DMA = 0x1,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_COMPRESS = 0x2,
        PRIV_DOCA_MLX5_OPC_MOD_WAIT_TIME = 0x2,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_DECOMPRESS = 0x3,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_REGEX = 0x4,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_CRYPTO = 0x6,
        PRIV_DOCA_MLX5_OPC_MOD_MMO_EC = 0x7,
    };

    // Send WQE sizes (in octowords)
    enum {
        PRIV_DOCA_MLX5_DS_NOP = 0x1,
        PRIV_DOCA_MLX5_DS_RDMA_ZERO_BYTE_COUNT = 0x2,
        PRIV_DOCA_MLX5_DS_WOD = 0x3,
        PRIV_DOCA_MLX5_DS_RDMA = 0x3,
        PRIV_DOCA_MLX5_DS_LOCAL_MMO = 0x4,
        PRIV_DOCA_MLX5_DS_ATOMIC = 0x4,
    };

    enum {
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN = 1 << 0,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR = 1 << 6,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_SIG_ERR = 1 << 9,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_BSF_ENABLE = 1 << 12,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_MKEY = 1 << 13,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_QPN = 1 << 14,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_LOCAL_WRITE = 1 << 18,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_READ = 1 << 19,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_WRITE = 1 << 20,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_ATOMIC = 1 << 21,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE = 1 << 29,
    };

    enum {
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_FLAG_INLINE = 1 << 7,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_FLAG_CHECK_FREE = 1 << 5,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET = 1 << 4,
        PRIV_DOCA_MLX5_WQE_UMR_CTRL_FLAG_CHECK_QPN = 1 << 3,
    };

    // MSVC can't compile member structs if they have [0] at the end and they are not the last member
    // See priv_doca_mlx5_wqe_mkey_context_seg above and it's usage in the code.
    struct priv_doca_mlx5_wqe_mkey_context_seg_member {
        uint8_t free;
        uint8_t reserved1;
        uint8_t access_flags;
        uint8_t sf;
        __be32 qpn_mkey;
        __be32 reserved2;
        __be32 flags_pd;
        __be64 start_addr;
        __be64 len;
        __be32 bsf_octword_size;
        __be32 reserved3[4];
        __be32 translations_octword_size;
        uint8_t reserved4[3];
        uint8_t log_page_size;
        __be32 reserved;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_mkey_context_seg_member) == 64,
        "struct priv_doca_mlx5_wqe_mkey_context_seg_member is not 64 bytes.");

    struct priv_doca_mlx5_umr_wqe {
        /* 1st WQEBB */
        struct priv_doca_mlx5_wqe_ctrl_seg general_ctrl_seg;
        struct priv_doca_mlx5_wqe_umr_ctrl_seg umr_ctrl_seg;
        /* 2nd WQEBB */
        struct priv_doca_mlx5_wqe_mkey_context_seg_member mkey_ctx_seg;
        /* 3rd WQEBB */
        struct priv_doca_mlx5_wqe_umr_klm_seg
            klm_arr[PRIV_DOCA_MLX5_SEND_WQE_BB / sizeof(struct priv_doca_mlx5_wqe_umr_klm_seg)];
    };
    static_assert(sizeof(struct priv_doca_mlx5_umr_wqe) == 192, "struct priv_doca_mlx5_umr_wqe is not 192 bytes.");

    struct priv_doca_mlx5_wqe_mmo_metadata_seg {
        __be32 mmo_control_31_0;
        __be32 local_key;
        __be64 local_address;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_mmo_metadata_seg) == 16,
        "struct priv_doca_mlx5_wqe_mmo_metadata_seg is not 16 bytes.");

    struct priv_doca_mlx5_mmo_wqe {
        struct priv_doca_mlx5_wqe_ctrl_seg ctrl;
        struct priv_doca_mlx5_wqe_mmo_metadata_seg mmo_meta;
        struct priv_doca_mlx5_wqe_data_seg src;
        struct priv_doca_mlx5_wqe_data_seg dest;
    };
    static_assert(sizeof(struct priv_doca_mlx5_mmo_wqe) == 64, "struct priv_doca_mlx5_mmo_wqe is not 64 bytes.");

    struct priv_doca_mlx5_wqe_ibl2_seg {
        uint8_t reserved_0[0x4];
        __be32 vl15_slr_icrc_fl_static_rate_sl;
        uint8_t reserved_63[0x4];
        __be32 dlid;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_ibl2_seg) == 16, "struct priv_doca_mlx5_wqe_ibl2_seg is not 16 bytes.");

    struct priv_doca_mlx5_send_ibl2_wqe {
        struct priv_doca_mlx5_wqe_ctrl_seg ctrl;
        struct priv_doca_mlx5_wqe_ibl2_seg ibl2;
        struct priv_doca_mlx5_wqe_data_seg data;
        /* dummy segment to align wqe to 64 bytes which is HW requirement */
        struct priv_doca_mlx5_wqe_data_seg dummy;
    };
    static_assert(sizeof(struct priv_doca_mlx5_send_ibl2_wqe) == 64,
        "struct priv_doca_mlx5_send_ibl2_wqe is not 64 bytes.");

    struct priv_doca_mlx5_wqe_transpose_seg {
        uint8_t reserved_0[0x3];
        uint8_t element_size;

        uint8_t reserved_20;
        uint8_t num_of_cols;
        uint8_t reserved_30;
        uint8_t num_of_rows;

        uint8_t reserved_40[0x8];
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_transpose_seg) == 16,
        "struct priv_doca_mlx5_wqe_transpose_seg is not 16 bytes.");

    struct priv_doca_mlx5_eth_wqe {
        struct priv_doca_mlx5_wqe_ctrl_seg ctrl;
        struct priv_doca_mlx5_wqe_eth_seg eseg;
        struct priv_doca_mlx5_wqe_data_seg dseg0;
        struct priv_doca_mlx5_wqe_data_seg dseg1;
    };
    static_assert(sizeof(struct priv_doca_mlx5_eth_wqe) == 64, "struct priv_doca_mlx5_eth_wqe is not 64 bytes.");

    struct priv_doca_mlx5_dump_wqe {
        struct priv_doca_mlx5_wqe_ctrl_seg cseg;
        struct priv_doca_mlx5_wqe_data_seg dseg;
    };
    static_assert(sizeof(struct priv_doca_mlx5_dump_wqe) == 32, "struct priv_doca_mlx5_dump_wqe is not 32 bytes.");

#define MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES 6
#define MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES 9

#define MLX5_WSEG_SIZE 16u

    /* Renaming structs wrt /usr/include/infiniband/mlx5dv.h for GPU support. */
    struct priv_doca_mlx5_wqe_mprq_next_seg {
        uint8_t rsvd0[2];
        __be16 next_wqe_index;
        uint8_t signature;
        uint8_t rsvd1[11];
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_mprq_next_seg) == 16,
        "struct priv_doca_mlx5_wqe_mprq_next_seg is not 16 bytes.");

    struct priv_doca_mlx5_wqe_mprq {
        struct priv_doca_mlx5_wqe_mprq_next_seg next_seg;
        struct priv_doca_mlx5_wqe_data_seg dseg;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_mprq) == 32, "struct priv_doca_mlx5_wqe_mprq is not 32 bytes.");

    struct priv_doca_mlx5_wqe_wseg {
        __be32 operation;
        __be32 lkey;
        __be32 va_high;
        __be32 va_low;
        __be64 value;
        __be64 mask;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_wseg) == 32, "struct priv_doca_mlx5_wqe_wseg is not 32 bytes.");

    struct priv_doca_mlx5_wqe_qseg {
        __be32 reserved0;
        __be32 reserved1;
        __be32 max_index;
        __be32 qpn_cqn;
    };
    static_assert(sizeof(struct priv_doca_mlx5_wqe_qseg) == 16, "struct priv_doca_mlx5_wqe_qseg is not 16 bytes.");

#define MLX5_ACCESS_REGISTER_DATA_DWORD_MAX 8u

    enum {
        PRIV_DOCA_MLX5_CQE_OWNER_MASK = 1,
    };

    enum {
        PRIV_DOCA_MLX5_CQE_REQ = 0x0,
        PRIV_DOCA_MLX5_CQE_RESP_WR_IMM = 0x1,
        PRIV_DOCA_MLX5_CQE_RESP_SEND = 0x2,
        PRIV_DOCA_MLX5_CQE_RESP_SEND_IMM = 0x3,
        PRIV_DOCA_MLX5_CQE_RESP_SEND_INV = 0x4,
        PRIV_DOCA_MLX5_CQE_RESIZE_CQ = 0x5,
        PRIV_DOCA_MLX5_CQE_NO_PACKET = 0x6,
        PRIV_DOCA_MLX5_CQE_SIGERR_ERR = 0xc,
        PRIV_DOCA_MLX5_CQE_REQ_ERR = 0xd,
        PRIV_DOCA_MLX5_CQE_RESP_ERR = 0xe,
        PRIV_DOCA_MLX5_CQE_INVALID = 0xf,
    };

    enum {
        PRIV_DOCA_MLX5_CQ_DB_REQ_NOT_SOL = 1 << 24,
        PRIV_DOCA_MLX5_CQ_DB_REQ_NOT = 0 << 24,
    };

    enum {
        PRIV_DOCA_MLX5_CQ_DOORBELL = 0x20
    };

    enum {
        PRIV_DOCA_MLX5_CQ_SET_CI = 0,
        PRIV_DOCA_MLX5_CQ_ARM_DB = 1,
    };

#define PRIV_DOCA_MLX5_CQE_OPCODE_SHIFT 4
#define PRIV_DOCA_MLX5_CQE_QPN_MASK 0xffffff
#define PRIV_DOCA_MLX5_CQE_USER_DATA_MASK 0xffffff

    struct priv_doca_mlx5_64b_cqe {
        struct {
            uint8_t reserved_at_0[2];
            __be16 wqe_id;

            uint8_t reserved_at_4[12];

            uint8_t reserved_at_16;
            uint8_t ml_path;
            uint8_t rsvd20[2];

            uint8_t rsvd22[2];
            __be16 slid;

            __be32 flags_rqpn;

            uint8_t hds_ip_ext;
            uint8_t l4_hdr_type_etc;
            __be16 vlan_info;
        };
        __be32 srqn_uidx;

        __be32 imm_inval_pkey;

        uint8_t app;
        uint8_t app_op;
        __be16 app_info;

        __be32 byte_cnt;

        __be64 timestamp;

        __be32 sop_drop_qpn;

        __be16 wqe_counter;
        uint8_t signature;
        uint8_t op_own;
    };
    static_assert(sizeof(struct priv_doca_mlx5_64b_cqe) == 64, "struct priv_doca_mlx5_64b_cqe is not 64 bytes.");

    struct priv_doca_mlx5_error_cqe {
        uint8_t rsvd0[54];
        uint8_t vendor_err_synd;
        uint8_t syndrome;
        __be32 s_wqe_opcode_qpn;
        __be16 wqe_counter;
        uint8_t rsvd62[2];
    };
    static_assert(sizeof(struct priv_doca_mlx5_error_cqe) == 64, "struct priv_doca_mlx5_error_cqe is not 64 bytes.");

    enum {
        PRIV_DOCA_MLX5_CMD_STAT_OK = 0x0,
        PRIV_DOCA_MLX5_CMD_STAT_INTERNAL_ERR = 0x1,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_OP_ERR = 0x2,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_PARAM_ERR = 0x3,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_SYS_STATE_ERR = 0x4,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_RESOURCE_ERR = 0x5,
        PRIV_DOCA_MLX5_CMD_STAT_RESOURCE_BUSY_ERR = 0x6,
        PRIV_DOCA_MLX5_CMD_STAT_EXCEED_LIM_ERR = 0x8,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_RES_STATE_ERR = 0x9,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_INDEX_ERR = 0xa,
        PRIV_DOCA_MLX5_CMD_STAT_NO_RESOURCES_ERR = 0xf,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_INPUT_LEN_ERR = 0x50,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_OUTPUT_LEN_ERR = 0x51,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_RESOURCE_STATE_ERR = 0x10,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_PKT_ERR = 0x30,
        PRIV_DOCA_MLX5_CMD_STAT_BAD_SIZE_OUTS_CQES_ERR = 0x40,
    };

    static inline const char *priv_doca_get_cmd_stat_str(int stat) {
        switch (stat) {
        case PRIV_DOCA_MLX5_CMD_STAT_OK:
            return "OK";
        case PRIV_DOCA_MLX5_CMD_STAT_INTERNAL_ERR:
            return "INTERNAL_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_OP_ERR:
            return "BAD_OP_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_PARAM_ERR:
            return "BAD_PARAM_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_SYS_STATE_ERR:
            return "BAD_SYS_STATE_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_RESOURCE_ERR:
            return "BAD_RESOURCE_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_RESOURCE_BUSY_ERR:
            return "RESOURCE_BUSY_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_EXCEED_LIM_ERR:
            return "EXCEED_LIM_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_RES_STATE_ERR:
            return "BAD_RES_STATE_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_INDEX_ERR:
            return "BAD_INDEX_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_NO_RESOURCES_ERR:
            return "NO_RESOURCES_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_INPUT_LEN_ERR:
            return "BAD_INPUT_LEN_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_OUTPUT_LEN_ERR:
            return "BAD_OUTPUT_LEN_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_RESOURCE_STATE_ERR:
            return "BAD_RESOURCE_STATE_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_PKT_ERR:
            return "BAD_PKT_ERR";
        case PRIV_DOCA_MLX5_CMD_STAT_BAD_SIZE_OUTS_CQES_ERR:
            return "BAD_SIZE_OUTS_CQES_ERR";
        default:
            return "UNKNOWN";
        }
    }

    enum {
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_PA = 0x0,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_MTT = 0x1,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_KLMS = 0x2,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_KSM = 0x3,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_SW_ICM = 0x4,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_MEMIC = 0x5,
        PRIV_DOCA_MLX5_MKC_ACCESS_MODE_CROSSING_VHCA_MKEY = 0x6,
    };

    enum {
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_GENERAL_DEVICE = 0x0 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_ETHERNET_OFFLOAD = 0x1 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_ATOMIC = 0x3 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_ROCE = 0x4 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_NIC_FLOW_TABLE = 0x7 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_ESW_FLOW_TABLE = 0x8 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_QOS = 0xc << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_ESW = 0x9 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_DEVICE_MEMORY = 0xf << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_IPSEC = 0x15 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_HOTPLUG = 0x18 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_CRYPTO = 0x1a << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_GENERAL_DEVICE_CAP_2 = 0x20 << 1,
        PRIV_DOCA_MLX5_HCA_CAP_OP_MOD_GENERIC_DEVICE_EMULATION = 0x27 << 1,
    };

    enum priv_doca_mlx5_cap_mode {
        PRIV_DOCA_MLX5_HCA_CAP_OPMOD_GET_MAX = 0,
        PRIV_DOCA_MLX5_HCA_CAP_OPMOD_GET_CUR = 1,
    };

    /* Page size is provided in granularity of 4K. */
#define PRIV_DOCA_MLX5_ADAPTER_PAGE_SHIFT 12

    enum {
        MLX5_EMULATION_HOTPLUG_STATE_HOTPLUG = 0x1,
        MLX5_EMULATION_HOTPLUG_STATE_HOTUNPLUG_PREPARE = 0x2,
        MLX5_EMULATION_HOTPLUG_STATE_POWER_OFF = 0x3,
        MLX5_EMULATION_HOTPLUG_STATE_POWER_ON = 0x4,
    };

    struct mlx5_ifc_generic_send_message_stateful_query_in_bits {
        uint8_t reserved_0[0x10];
        uint8_t region_id[0x10];
        uint8_t reserved_20[0x20];
    };

    struct mlx5_ifc_generic_send_message_stateful_modify_in_bits {
        uint8_t data_length[0x10];
        uint8_t region_id[0x10];
        uint8_t reserved_20[0x10];
        uint8_t offset_in_region[0x10];
        uint8_t data[0x0][0x20];
    };

    struct mlx5_ifc_generic_send_message_stateful_set_default_in_bits {
        uint8_t data_length[0x10];
        uint8_t region_id[0x10];
        uint8_t reserved_20[0x20];
        uint8_t data[0x0][0x20];
    };

    /* Get CQE owner bit */
#define PRIV_DOCA_MLX5_CQE_GET_OWNER(op_own) ((op_own)&PRIV_DOCA_MLX5_CQE_OWNER_MASK)

/* Get CQE opcode. */
#define PRIV_DOCA_MLX5_CQE_GET_OPCODE(op_own) (((op_own)&0xf0) >> PRIV_DOCA_MLX5_CQE_OPCODE_SHIFT)

#define PRIV_DOCA_MLX5_ETH_WQE_L3_CSUM (1 << 6)
#define PRIV_DOCA_MLX5_ETH_WQE_L4_CSUM (1 << 7)

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* PRIV_DOCA_MLX5_PRM_MANUAL_H_ */
