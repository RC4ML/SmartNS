#pragma once

#include <gflags/gflags.h>

DECLARE_string(deviceName);

DECLARE_uint64(gidIndex);

DECLARE_uint64(batch_size);

DECLARE_uint64(qp_per_core);

DECLARE_uint64(outstanding);

DECLARE_uint64(port);

DECLARE_uint64(numaNode);

DECLARE_uint64(coreOffset);

DECLARE_string(serverIp);

DECLARE_bool(is_server);

DECLARE_uint64(payload_size);

DECLARE_uint64(iterations);