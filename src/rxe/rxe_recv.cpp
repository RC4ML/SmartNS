#include "rxe/rxe.h"
#include "rxe/rxe_hdr.h"
#include "rxe/rxe_opcode.h"
#include "rxe/rxe_param.h"
#include "raw_packet/raw_packet.h"
#include "smartns.h"

void send_ack(datapath_handler *handler, dpu_qp *qp, uint8_t syndrome, uint32_t psn) {
    void *header_addr = handler->txpath_handler->get_next_pktheader_addr();
    struct rxe_bth *bth = reinterpret_cast<rxe_bth *>(reinterpret_cast<size_t>(header_addr) + sizeof(udp_packet));
    int header_size = 64;

    bth->opcode = IB_OPCODE_RC_ACKNOWLEDGE;
    bth->flags = 0;
    bth->pkey = 0xFFFF;
    bth->qpn = qp->remote_qp_number & BTH_QPN_MASK;
    bth->apsn = psn;

    rxe_aeth *aeth = reinterpret_cast<rxe_aeth *>(bth + 1);
    aeth->smsn = (AETH_SYN_MASK & (syndrome << 24)) | (AETH_MSN_MASK & qp->recv_wq->msn);

    handler->txpath_handler->commit_pkt_without_payload(header_size);
}

void complete_send_wqe(datapath_handler *handler, dpu_qp *qp, dpu_send_wqe *wqe) {
    if (wqe->state == dpu_send_wqe_state_pending) {
        if (psn_compare(wqe->last_psn, qp->recv_wq->psn) >= 0) {
            qp->recv_wq->psn = (wqe->last_psn + 1) & BTH_PSN_MASK;
            qp->recv_wq->opcode = -1;
        }
    }
    bool post = wqe->is_signal;

    if (post) {
        smartns_cqe *cqe = qp->send_cq->get_next_cqe();
        cqe->byte_count = wqe->byte_count;
        cqe->cq_opcode = MLX5_CQE_REQ;
        cqe->mlx5_opcode = 0;
        cqe->op_own = qp->send_cq->own_flag;
        cqe->qpn = qp->qp_number;
        cqe->wqe_counter = wqe->cur_pos;

        handler->dma_send_cq_to_host(qp);
    }

    qp->send_wq->step_tail();
    if (qp->send_wq->is_empty()) {
        handler->active_qp_list.erase(qp);
    }
}

int rxe_handle_recv(datapath_handler *handler) {
    int recv = ibv_poll_cq(handler->rxpath_handler->recv_cq, CTX_POLL_BATCH, handler->wc_send_recv);

    for (int i = 0;i < recv;i++) {
        if (handler->wc_send_recv[i].status != IBV_WC_SUCCESS || handler->wc_send_recv[i].opcode != IBV_WC_RECV) {
            fprintf(stderr, "Recv error %d\n", handler->wc_send_recv[i].status);
            exit(1);
        }
        struct rxe_bth *bth = reinterpret_cast<struct rxe_bth *>(handler->wc_send_recv[i].wr_id + sizeof(udp_packet));
        uint8_t opcode = bth->opcode;
        uint32_t psn = BTH_PSN_MASK & bth->apsn;
        uint32_t local_qpn = bth->qpn & BTH_QPN_MASK;
        dpu_qp *qp = handler->local_qpn_to_qp_list[local_qpn];
        assert(qp);

        int mask = rxe_opcode[opcode].mask;

        if (mask & RXE_REQ_MASK) {
            uint32_t payload_size = handler->wc_send_recv[i].byte_len - sizeof(udp_packet) - rxe_opcode[opcode].offset[RXE_PAYLOAD];
            int diff = psn_compare(psn, qp->recv_wq->psn);
            if (diff > 0) {
                if (qp->recv_wq->sent_psn_nak == 1) {
                    continue;
                }
                qp->recv_wq->sent_psn_nak = 1;
                send_ack(handler, qp, AETH_NAK_PSN_SEQ_ERROR, qp->recv_wq->psn);
                continue;
            } else if (diff < 0) {
                uint32_t prev_psn = (qp->recv_wq->ack_psn - 1) & BTH_PSN_MASK;
                if (mask & RXE_SEND_MASK || mask & RXE_WRITE_MASK) {
                    send_ack(handler, qp, AETH_ACK_UNLIMITED, prev_psn);
                    continue;
                } else {
                    fprintf(stderr, "TODO waiting for implementation\n");
                    exit(1);
                }
            }
            if (qp->recv_wq->sent_psn_nak) {
                qp->recv_wq->sent_psn_nak = 0;
            }
            if (mask & (RXE_READ_MASK | RXE_WRITE_MASK)) {
                rxe_reth *reth = reinterpret_cast<rxe_reth *>(reinterpret_cast<char *>(bth) + rxe_opcode[opcode].offset[RXE_RETH]);
                if (mask & RXE_RETH_MASK) {
                    qp->recv_wq->host_va = reth->va;
                    qp->recv_wq->offset = 0;
                    qp->recv_wq->host_rkey = reth->rkey;
                    qp->recv_wq->byte_count = reth->len;
                    qp->recv_wq->resid = reth->len;
                    qp->recv_wq->mr = qp->dpu_ctx->mr_list[reth->rkey];
                    assert(qp->recv_wq->mr);
                }
            }

            // execute the operation
            if (mask & RXE_SEND_MASK) {
                handler->dma_send_payload_to_host(qp, reinterpret_cast<void *>(reinterpret_cast<char *>(bth) + rxe_opcode[opcode].offset[RXE_PAYLOAD]), payload_size);
            } else if (mask & RXE_WRITE_MASK) {
                handler->dma_write_payload_to_host(qp, reinterpret_cast<void *>(reinterpret_cast<char *>(bth) + rxe_opcode[opcode].offset[RXE_PAYLOAD]), payload_size);
            } else if (mask & RXE_READ_MASK) {
                fprintf(stderr, "TODO waiting for implementation\n");
                exit(1);
            }

            qp->recv_wq->psn = (psn + 1) & BTH_PSN_MASK;
            qp->recv_wq->ack_psn = qp->recv_wq->psn;
            qp->recv_wq->opcode = opcode;

            if (mask & RXE_COMP_MASK) {
                qp->recv_wq->msn++;
                smartns_cqe *cqe = qp->recv_cq->get_next_cqe();
                cqe->byte_count = qp->recv_wq->now_total_dma_byte;
                cqe->cq_opcode = MLX5_CQE_RESP_SEND;
                cqe->mlx5_opcode = 0;
                cqe->op_own = qp->recv_cq->own_flag;
                cqe->qpn = qp->qp_number;
                cqe->wqe_counter = qp->recv_wq->head;

                handler->dma_recv_cq_to_host(qp);
            }

            if (bth->apsn & BTH_ACK_MASK) {
                send_ack(handler, qp, AETH_ACK_UNLIMITED, psn);
            }
            // recv ack or nack packet
        } else {
            dpu_send_wq *send_wq = qp->send_wq;

            while (true) {
                dpu_send_wqe *send_wqe = send_wq->get_wqe(send_wq->tail);
                if (send_wq->is_empty() || send_wqe->state == dpu_send_wqe_state_posted) {
                    break;
                }
                int diff = psn_compare(psn, send_wqe->last_psn);
                if (diff > 0) {
                    if (send_wqe->state == dpu_send_wqe_state_pending) {
                        complete_send_wqe(handler, qp, send_wqe);
                        continue;
                    } else {
                        fprintf(stderr, "TODO waiting for implementation\n");
                        exit(1);
                    }
                }
                diff = psn_compare(psn, qp->comp_info->psn);
                if (diff < 0) {
                    if (psn == send_wqe->last_psn) {
                        complete_send_wqe(handler, qp, send_wqe);
                        break;
                    } else {
                        break;
                    }
                }

                assert(opcode == IB_OPCODE_RC_ACKNOWLEDGE);
                rxe_aeth *aeth = reinterpret_cast<rxe_aeth *>(bth + 1);
                uint8_t syn = (AETH_SYN_MASK & aeth->smsn) >> 24;
                switch (syn & AETH_TYPE_MASK) {
                case AETH_ACK:
                    break;
                case AETH_NAK:
                    fprintf(stderr, "TODO waiting for implementation\n");
                    exit(1);
                }

                if (send_wqe->state == dpu_send_wqe_state_pending && send_wqe->last_psn == psn) {
                    complete_send_wqe(handler, qp, send_wqe);
                }

                if (mask & RXE_END_MASK) {
                    qp->comp_info->opcode = -1;
                } else {
                    qp->comp_info->opcode = opcode;
                }
                qp->comp_info->psn = (psn + 1) & BTH_PSN_MASK;
                break;
            }
        }
    }
    handler->txpath_handler->commit_flush();

    uint32_t dma_finish = handler->dma_handler->poll_dma_cq();
    while (dma_finish) {
        uint32_t now_post_recv = min_(SMARTNS_RX_BATCH, dma_finish);
        for (uint32_t i = 0;i < now_post_recv;i++) {
            handler->rxpath_handler->recv_sge_list[i * SMARTNS_RX_SEG].addr = handler->rxpath_handler->recv_offset_handler.offset() + handler->rxpath_handler->recv_buf_addr;
            handler->rxpath_handler->recv_sge_list[i * SMARTNS_RX_SEG].length = SMARTNS_RX_PACKET_BUFFER;
            handler->rxpath_handler->recv_wr[i].wr_id = handler->rxpath_handler->recv_offset_handler.offset() + handler->rxpath_handler->recv_buf_addr;
            handler->rxpath_handler->recv_wr[i].next = nullptr;

            if (i > 0) {
                handler->rxpath_handler->recv_wr[i - 1].next = handler->rxpath_handler->recv_wr + i;
            }
            handler->rxpath_handler->recv_offset_handler.step();
        }
        assert(ibv_post_wq_recv(handler->rxpath_handler->recv_wq, handler->rxpath_handler->recv_wr, &handler->rxpath_handler->recv_bad_wr) == 0);
        dma_finish -= now_post_recv;
    }

    return recv;
}