#pragma once
#include <stdint.h>
struct rxe_bth {
    uint8_t			opcode;
    uint8_t			flags;
    uint16_t			pkey;
    uint32_t			qpn;
    uint32_t			apsn;
};
#define BTH_TVER		(0)
#define BTH_DEF_PKEY		(0xffff)

#define BTH_SE_MASK		(0x80)
#define BTH_MIG_MASK		(0x40)
#define BTH_PAD_MASK		(0x30)
#define BTH_TVER_MASK		(0x0f)
#define BTH_FECN_MASK		(0x80000000)
#define BTH_BECN_MASK		(0x40000000)
#define BTH_RESV6A_MASK		(0x3f000000)
#define BTH_QPN_MASK		(0x00ffffff)
#define BTH_ACK_MASK		(0x80000000)
#define BTH_RESV7_MASK		(0x7f000000)
#define BTH_PSN_MASK		(0x00ffffff)

struct rxe_rdeth {
    uint32_t			een;
};

#define RDETH_EEN_MASK		(0x00ffffff)

struct rxe_aeth {
    uint32_t			smsn;
};

#define AETH_SYN_MASK		(0xff000000)
#define AETH_MSN_MASK		(0x00ffffff)

enum aeth_syndrome {
    AETH_TYPE_MASK = 0xe0,
    AETH_ACK = 0x00,
    AETH_RNR_NAK = 0x20,
    AETH_RSVD = 0x40,
    AETH_NAK = 0x60,
    AETH_ACK_UNLIMITED = 0x1f,
    AETH_NAK_PSN_SEQ_ERROR = 0x60,
    AETH_NAK_INVALID_REQ = 0x61,
    AETH_NAK_REM_ACC_ERR = 0x62,
    AETH_NAK_REM_OP_ERR = 0x63,
    AETH_NAK_INV_RD_REQ = 0x64,
};

struct rxe_atmack {
    uint64_t			orig;
};

struct rxe_deth {
    uint32_t			qkey;
    uint32_t			sqp;
};
#define GSI_QKEY		(0x80010000)
#define DETH_SQP_MASK		(0x00ffffff)

struct rxe_immdt {
    uint32_t			imm;
};
struct rxe_ieth {
    uint32_t			rkey;
};

struct rxe_reth {
    uint64_t			va;
    uint32_t			rkey;
    uint32_t			len;
};

struct rxe_atmeth {
    uint64_t			va;
    uint32_t			rkey;
    uint64_t			swap_add;
    uint64_t			comp;
} __attribute__((packed));


enum rxe_hdr_length {
    RXE_BTH_BYTES = sizeof(struct rxe_bth),
    RXE_DETH_BYTES = sizeof(struct rxe_deth),
    RXE_IMMDT_BYTES = sizeof(struct rxe_immdt),
    RXE_RETH_BYTES = sizeof(struct rxe_reth),
    RXE_AETH_BYTES = sizeof(struct rxe_aeth),
    RXE_ATMACK_BYTES = sizeof(struct rxe_atmack),
    RXE_ATMETH_BYTES = sizeof(struct rxe_atmeth),
    RXE_IETH_BYTES = sizeof(struct rxe_ieth),
    RXE_RDETH_BYTES = sizeof(struct rxe_rdeth),
};

