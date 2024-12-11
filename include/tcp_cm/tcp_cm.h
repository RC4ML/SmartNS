#pragma once
#include "common.hpp"

class tcp_param {
public:
    bool isServer;
    std::string serverIp;
    // only used for server
    int sockfd;
    // used for server and client
    int connfd;
    int sock_port;
};

void socket_init(tcp_param &net_param);
void exchange_data(tcp_param &net_param, char *local_data, char *remote_data, size_t data_size);
