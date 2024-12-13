#include "smartns_kernel.h"

unsigned char SMARTNS_BF_IP_ADDR[5] = { 192,168,100,2,'\0' };

u32 create_address(u8 *ip) {
    u32 addr = 0;
    int i;

    for (i = 0; i < 4; i++) {
        addr += ip[i];
        if (i == 3)
            break;
        addr <<= 8;
    }
    return addr;
}

int tcp_connect_to_bf(void) {
    int ret = -1;

    struct sockaddr_in saddr;

    ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &global_tcp_socket);
    if (ret < 0) {
        pr_info("Error: %d while creating first socket.\n", ret);
        return EIO;
    }

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(SMARTNS_BF_TCP_PORT);
    saddr.sin_addr.s_addr = htonl(create_address(SMARTNS_BF_IP_ADDR));

    ret = global_tcp_socket->ops->connect(global_tcp_socket, (struct sockaddr *)&saddr\
        , sizeof(saddr), O_RDWR);
    if (ret && (ret != -EINPROGRESS)) {
        pr_info("Error: %d while connecting using conn socket.\n", ret);
        return EIO;
    }

    return 0;
}

int tcp_client_send(struct socket *sock, const char *buf, const size_t length, \
    unsigned long flags) {
    struct msghdr msg;
    //struct iovec iov;
    struct kvec vec;
    int len, written = 0, left = length;

    msg.msg_name = 0;
    msg.msg_namelen = 0;

    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;

repeat_send:
    vec.iov_len = left;
    vec.iov_base = (char *)buf + written;

    len = kernel_sendmsg(sock, &msg, &vec, left, left);
    if ((len == -ERESTARTSYS) || (!(flags & MSG_DONTWAIT) && \
        (len == -EAGAIN)))
        goto repeat_send;
    if (len > 0) {
        written += len;
        left -= len;
        if (left)
            goto repeat_send;
    }

    return written ? written : len;
}

int tcp_client_receive(struct socket *sock, char *str, \
    unsigned long flags) {
    struct msghdr msg;
    struct kvec vec;
    int len;
    int max_size = SMARTNS_MSG_SIZE;

    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;
    vec.iov_len = max_size;
    vec.iov_base = str;

read_again:
    len = kernel_recvmsg(sock, &msg, &vec, max_size, max_size, flags);

    if (len == -EAGAIN || len == -ERESTARTSYS) {
        pr_info("error while reading: %d\n", len);

        goto read_again;
    }

    return len;
}
