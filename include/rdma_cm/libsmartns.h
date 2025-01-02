#pragma once

#include "libr.h"

void smartns_roce_init(rdma_param &rdma_param, int num_contexts);

qp_handler *smartns_create_qp_rc(rdma_param &rdma_param, void *buf, size_t size, struct pingpong_info *info, int context_index, int qp_index);

void smartns_connect_qp_rc(rdma_param &rdma_param, qp_handler &qp_handler, struct pingpong_info *remote_info, struct pingpong_info *local_info);

void smartns_post_send(qp_handler &qp_handler, size_t offset, int length);

void smartns_post_send_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

void smartns_post_recv(qp_handler &qp_handler, size_t offset, int length);

void smartns_post_recv_batch(qp_handler &qp_handler, int batch_size, offset_handler &handler, int length);

int smartns_poll_send_cq(qp_handler &qp_handler, struct ibv_wc *wc);

int smartns_poll_recv_cq(qp_handler &qp_handler, struct ibv_wc *wc);