#include "gflags_common.h"
#include "common.hpp"

DEFINE_string(deviceName, "mlx5_0", "deviceName");

DEFINE_uint64(gidIndex, 3, "gidIndex");

DEFINE_uint64(batch_size, 1, "requeset batch_size");

DEFINE_uint64(qp_per_core, 1, "qp per core");

DEFINE_uint64(outstanding, 32, "outstanding request");

DEFINE_uint64(port, 6666, "bind_port");

DEFINE_uint64(numaNode, 0, "numa node");

DEFINE_uint64(coreOffset, 0, "core offset");

DEFINE_string(serverIp, "", "server ip");

DEFINE_bool(is_server, false, "is server or client");

DEFINE_uint64(payload_size, 1024, "packet payload size");

DEFINE_uint64(iterations, 1000, "iterations");

static bool ValidateDeviceName(const char *flag_name, const std::string &value) {
    if (value.empty()) {
        return false;
    }
    return value.find("mlx5") != std::string::npos;
}

static bool ValidateGidIndex(const char *flag_name, uint64_t value) {
    if (value < 0 || value > 3) {
        return false;
    }
    return true;
}

static bool ValidateBatchSize(const char *flag_name, uint64_t value) {
    if (value < 1 || value > 32) {
        return false;
    }
    return true;
}

static bool ValidateQpPerCore(const char *flag_name, uint64_t value) {
    if (value < 1 || value > 64) {
        return false;
    }
    return true;
}

static bool ValidateOutstanding(const char *flag_name, uint64_t value) {
    if (value < 1 || value > 1024) {
        return false;
    }
    return true;
}

static bool ValidatePort(const char *flag_name, uint64_t value) {
    if (value < 1024 || value > 65535) {
        return false;
    }
    return true;
}

static bool ValidateNumaNode(const char *flag_name, uint64_t value) {
    if (value < 0 || value > 1) {
        return false;
    }
    return true;
}

static bool ValidateIp(const char *flag_name, const std::string &value) {
    if (value.empty()) {
        return true;
    }
    const std::regex re(R"(^(\d{1,3}\.){3}\d{1,3}:\d+$)");

    if (!std::regex_match(value, re)) {
        return false;
    }
    return true;
}

DEFINE_validator(deviceName, &ValidateDeviceName);
DEFINE_validator(gidIndex, &ValidateGidIndex);
DEFINE_validator(batch_size, &ValidateBatchSize);
DEFINE_validator(qp_per_core, &ValidateQpPerCore);
DEFINE_validator(outstanding, &ValidateOutstanding);
DEFINE_validator(port, &ValidatePort);
DEFINE_validator(numaNode, &ValidateNumaNode);
DEFINE_validator(serverIp, &ValidateIp);
