#include "rxe/rxe_opcode.h"
#include "rxe/rxe_hdr.h"

#ifdef __cplusplus
extern "C" {
#endif

    struct rxe_opcode_info rxe_opcode[RXE_NUM_OPCODE] = {
        [IB_OPCODE_RC_SEND_FIRST] = {
            .name = "IB_OPCODE_RC_SEND_FIRST",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_RWR_MASK
                    | RXE_SEND_MASK | RXE_START_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_MIDDLE] = {
            .name = "IB_OPCODE_RC_SEND_MIDDLE]",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_SEND_MASK
                    | RXE_MIDDLE_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_LAST] = {
            .name = "IB_OPCODE_RC_SEND_LAST",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_COMP_MASK
                    | RXE_SEND_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE] = {
            .name = "IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE",
            .mask = RXE_IMMDT_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_COMP_MASK | RXE_SEND_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_IMMDT_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_IMMDT] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_IMMDT_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_ONLY] = {
            .name = "IB_OPCODE_RC_SEND_ONLY",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_COMP_MASK
                    | RXE_RWR_MASK | RXE_SEND_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE] = {
            .name = "IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE",
            .mask = RXE_IMMDT_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_COMP_MASK | RXE_RWR_MASK | RXE_SEND_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_IMMDT_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_IMMDT] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_IMMDT_BYTES,
            }
        },
        [IB_OPCODE_RC_ACKNOWLEDGE] = {
            .name = "IB_OPCODE_RC_ACKNOWLEDGE",
            .mask = RXE_AETH_MASK | RXE_ACK_MASK | RXE_START_MASK
                    | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_AETH_BYTES, // used for timestamp
            .offset = {
                [RXE_BTH] = 0,
                [RXE_AETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_AETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_FIRST] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_FIRST",
            .mask = RXE_RETH_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_WRITE_MASK | RXE_START_MASK,
            .length = RXE_BTH_BYTES + RXE_RETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_RETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_RETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_MIDDLE] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_MIDDLE",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_WRITE_MASK
                    | RXE_MIDDLE_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_LAST] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_LAST",
            .mask = RXE_PAYLOAD_MASK | RXE_REQ_MASK | RXE_WRITE_MASK
                    | RXE_END_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE",
            .mask = RXE_IMMDT_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_WRITE_MASK | RXE_COMP_MASK | RXE_RWR_MASK
                    | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_IMMDT_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_IMMDT] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_IMMDT_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_ONLY] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_ONLY",
            .mask = RXE_RETH_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_WRITE_MASK | RXE_START_MASK
                    | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_RETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_RETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_RETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE] = {
            .name = "IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE",
            .mask = RXE_RETH_MASK | RXE_IMMDT_MASK | RXE_PAYLOAD_MASK
                    | RXE_REQ_MASK | RXE_WRITE_MASK
                    | RXE_COMP_MASK | RXE_RWR_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_IMMDT_BYTES + RXE_RETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_RETH] = RXE_BTH_BYTES,
                [RXE_IMMDT] = RXE_BTH_BYTES
                            + RXE_RETH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_RETH_BYTES
                            + RXE_IMMDT_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_READ_REQUEST] = {
            .name = "IB_OPCODE_RC_RDMA_READ_REQUEST",
            .mask = RXE_RETH_MASK | RXE_REQ_MASK | RXE_READ_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_RETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_RETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_RETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST] = {
            .name = "IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST",
            .mask = RXE_AETH_MASK | RXE_PAYLOAD_MASK | RXE_ACK_MASK
                    | RXE_START_MASK,
            .length = RXE_BTH_BYTES + RXE_AETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_AETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_AETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE] = {
            .name = "IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE",
            .mask = RXE_PAYLOAD_MASK | RXE_ACK_MASK | RXE_MIDDLE_MASK,
            .length = RXE_BTH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_PAYLOAD] = RXE_BTH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST] = {
            .name = "IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST",
            .mask = RXE_AETH_MASK | RXE_PAYLOAD_MASK | RXE_ACK_MASK
                    | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_AETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_AETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_AETH_BYTES,
            }
        },
        [IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY] = {
            .name = "IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY",
            .mask = RXE_AETH_MASK | RXE_PAYLOAD_MASK | RXE_ACK_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_AETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_AETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_AETH_BYTES,
            }
        },
        [IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE] = {
            .name = "IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE",
            .mask = RXE_AETH_MASK | RXE_ATMACK_MASK | RXE_ACK_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_ATMACK_BYTES + RXE_AETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_AETH] = RXE_BTH_BYTES,
                [RXE_ATMACK] = RXE_BTH_BYTES
                            + RXE_AETH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                        + RXE_ATMACK_BYTES + RXE_AETH_BYTES,
            }
        },
        [IB_OPCODE_RC_COMPARE_SWAP] = {
            .name = "IB_OPCODE_RC_COMPARE_SWAP",
            .mask = RXE_ATMETH_MASK | RXE_REQ_MASK | RXE_ATOMIC_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_ATMETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_ATMETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_ATMETH_BYTES,
            }
        },
        [IB_OPCODE_RC_FETCH_ADD] = {
            .name = "IB_OPCODE_RC_FETCH_ADD",
            .mask = RXE_ATMETH_MASK | RXE_REQ_MASK | RXE_ATOMIC_MASK
                    | RXE_START_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_ATMETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_ATMETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_ATMETH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE] = {
            .name = "IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE",
            .mask = RXE_IETH_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_COMP_MASK | RXE_SEND_MASK | RXE_END_MASK,
            .length = RXE_BTH_BYTES + RXE_IETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_IETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_IETH_BYTES,
            }
        },
        [IB_OPCODE_RC_SEND_ONLY_WITH_INVALIDATE] = {
            .name = "IB_OPCODE_RC_SEND_ONLY_INV",
            .mask = RXE_IETH_MASK | RXE_PAYLOAD_MASK | RXE_REQ_MASK
                    | RXE_COMP_MASK | RXE_RWR_MASK | RXE_SEND_MASK
                    | RXE_END_MASK | RXE_START_MASK,
            .length = RXE_BTH_BYTES + RXE_IETH_BYTES,
            .offset = {
                [RXE_BTH] = 0,
                [RXE_IETH] = RXE_BTH_BYTES,
                [RXE_PAYLOAD] = RXE_BTH_BYTES
                            + RXE_IETH_BYTES,
            }
        },
    };

#ifdef __cplusplus
}
#endif