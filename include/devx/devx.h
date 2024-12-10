#pragma once

#include "common.hpp"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEVX_OK (0)
#define DEVX_FAILED (-1)
#define DEVX_VUID_BYTES 128

    struct devx_klm {
        uint32_t byte_count;
        uint32_t mkey;
        uint64_t address;
    };


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
