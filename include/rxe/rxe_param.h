#pragma once

enum rxe_device_param {
    RXE_MAX_PKT_PER_ACK = 64,
    RXE_MAX_UNACKED_PSNS = 128,
};

static inline int psn_compare(uint32_t psn_a, uint32_t psn_b) {
    int diff;

    diff = (psn_a - psn_b) << 8;
    return diff;
}
