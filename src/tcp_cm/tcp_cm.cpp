#include "tcp_cm/tcp_cm.h"
#include "netdb.h"

void socket_init(tcp_param &net_param) {
    if (net_param.sock_port == 0) {
        net_param.sock_port = 6666;
    }

    if (net_param.isServer) {
        addrinfo hints{};
        addrinfo *server_address{ nullptr };
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        assert(getaddrinfo(NULL, std::to_string(net_param.sock_port).c_str(), &hints, &server_address) >= 0);
        auto sockfd = socket(server_address->ai_family, server_address->ai_socktype, server_address->ai_protocol);
        assert(sockfd > 0);
        int reuse = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        assert(bind(sockfd, server_address->ai_addr, server_address->ai_addrlen) == 0);
        free(server_address);
        assert(listen(sockfd, 8) == 0);
        net_param.sockfd = sockfd;

        net_param.connfd = accept(sockfd, NULL, 0);
        assert(net_param.connfd >= 0);

        SMARTNS_INFO("TCP connected\n");
    } else {
        sleep(1);
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *server_address{ nullptr };
        assert(getaddrinfo(net_param.serverIp.c_str(), std::to_string(net_param.sock_port).c_str(), &hints, &server_address) >= 0);
        int connfd = socket(server_address->ai_family, server_address->ai_socktype, server_address->ai_protocol);
        assert(connfd > 0);
        assert(connect(connfd, server_address->ai_addr, server_address->ai_addrlen) == 0);
        freeaddrinfo(server_address);

        net_param.connfd = connfd;
    }
}

void exchange_data(tcp_param &net_param, char *local_data, char *remote_data, size_t data_size) {
    size_t dummy;
    (void)dummy;
    if (net_param.isServer) {
        dummy = read(net_param.connfd, remote_data, data_size);
        dummy = write(net_param.connfd, local_data, data_size);
    } else {
        dummy = write(net_param.connfd, local_data, data_size);
        dummy = read(net_param.connfd, remote_data, data_size);
    }
    SMARTNS_INFO("TCP exchange data size: %ld\n", data_size);
}