#include "rxe/rxe.h"
#include "rxe/rxe_hdr.h"
#include "rxe/rxe_opcode.h"
#include "rxe/rxe_param.h"
#include "raw_packet/raw_packet.h"
#include "smartns.h"


int next_opcode(dpu_qp *qp, dpu_send_wqe *wqe, int opcode) {
    int fits = (wqe->byte_count - wqe->cur_pkt_offset) <= static_cast<uint32_t>(qp->mtu);

    switch (opcode) {
    case IBV_WR_SEND:
        if (qp->send_wq->opcode == IB_OPCODE_RC_SEND_FIRST || qp->send_wq->opcode == IB_OPCODE_RC_SEND_MIDDLE) {
            return fits ? IB_OPCODE_RC_SEND_LAST : IB_OPCODE_RC_SEND_MIDDLE;
        } else {
            return fits ? IB_OPCODE_RC_SEND_ONLY : IB_OPCODE_RC_SEND_FIRST;
        }
    case IBV_WR_RDMA_WRITE:
        if (qp->send_wq->opcode == IB_OPCODE_RC_RDMA_WRITE_FIRST || qp->send_wq->opcode == IB_OPCODE_RC_RDMA_WRITE_MIDDLE) {
            return fits ? IB_OPCODE_RC_RDMA_WRITE_LAST : IB_OPCODE_RC_RDMA_WRITE_MIDDLE;
        } else {
            return fits ? IB_OPCODE_RC_RDMA_WRITE_ONLY : IB_OPCODE_RC_RDMA_WRITE_FIRST;
        }
    case IBV_WR_RDMA_READ:
        return IB_OPCODE_RC_RDMA_READ_REQUEST;
    }
    assert(false);
}

void init_req_packet(datapath_handler *handler, dpu_qp *qp, dpu_send_wqe *wqe, int opcode, int payload) {
    int header_size = rxe_opcode[opcode].length + sizeof(udp_packet);
    int mask = rxe_opcode[opcode].mask;
    uint32_t psn = qp->send_wq->psn;

    uint32_t remote_qpn = qp->remote_qp_number;
    int ack_req = (mask & RXE_END_MASK) || (++qp->send_wq->noack_pkts > RXE_MAX_PKT_PER_ACK);
    if (ack_req) {
        qp->send_wq->noack_pkts = 0;
    }

    void *header_addr = handler->txpath_handler->get_next_pktheader_addr();
    struct rxe_bth *bth = reinterpret_cast<rxe_bth *>(reinterpret_cast<size_t>(header_addr) + sizeof(udp_packet));

    bth->opcode = opcode;
    bth->flags = 0;
    bth->pkey = 0xFFFF;
    bth->qpn = remote_qpn & BTH_QPN_MASK;
    psn &= BTH_PSN_MASK;
    if (ack_req) {
        psn |= BTH_ACK_MASK;
    }
    bth->apsn = psn;

    if (mask & RXE_RETH_MASK) {
        struct rxe_reth *reth = reinterpret_cast<rxe_reth *>(reinterpret_cast<size_t>(bth) + rxe_opcode[opcode].offset[RXE_RETH]);
        reth->rkey = wqe->remote_rkey;
        reth->va = wqe->remote_addr;
        reth->len = wqe->byte_count;
    }

    if (mask & RXE_WRITE_OR_SEND) {
        handler->txpath_handler->commit_pkt_with_payload(wqe->remote_addr + wqe->cur_pkt_offset, wqe->remote_rkey, header_size, payload);
    } else {
        handler->txpath_handler->commit_pkt_without_payload(header_size);
    }

}

int rxe_handle_req(datapath_handler *handler, dpu_qp *qp) {
    dpu_send_wq *send_wq = qp->send_wq;
    int total_send = 0;
    assert(!send_wq->is_empty());
    for (;send_wq->wqe_index != send_wq->head;) {
        // TODO
        dpu_send_wqe *send_wqe = send_wq->get_wqe(send_wq->wqe_index);
        if (send_wqe->state == dpu_send_wqe_state_done || send_wqe->state == dpu_send_wqe_state_pending || send_wqe->cur_pkt_offset == send_wqe->byte_count) {
            send_wq->step_wqe_index();
            continue;
        }

        if (unlikely(psn_compare(qp->send_wq->psn, (qp->comp_info->psn + RXE_MAX_UNACKED_PSNS)) > 0)) {
            return total_send;
        }

        int opcode = next_opcode(qp, send_wqe, send_wqe->opcode);
        int mask = rxe_opcode[opcode].mask;
        int payload = (mask & RXE_WRITE_OR_SEND) ? send_wqe->byte_count - send_wqe->cur_pkt_offset : 0;
        if (payload >= qp->mtu) {
            payload = qp->mtu;
        }
        init_req_packet(handler, qp, send_wqe, opcode, payload);
        total_send++;

        send_wqe->cur_pkt_offset += payload;
        send_wqe->cur_pkt_num += 1;

        if (mask & RXE_END_MASK) {
            send_wqe->state = dpu_send_wqe_state_pending;
        } else {
            send_wqe->state = dpu_send_wqe_state_processing;
        }

        int num_pkt = (send_wqe->byte_count + qp->mtu - 1) / qp->mtu;
        if (num_pkt == 0) {
            num_pkt = 1;
        }

        if (mask & RXE_START_MASK) {
            send_wqe->first_psn = qp->send_wq->psn;
            send_wqe->last_psn = (qp->send_wq->psn + num_pkt - 1) & BTH_PSN_MASK;
        }

        if (mask & RXE_READ_MASK) {
            qp->send_wq->psn = (send_wqe->first_psn + num_pkt) & BTH_PSN_MASK;
        } else {
            qp->send_wq->psn = (qp->send_wq->psn + 1) & BTH_PSN_MASK;
        }

        qp->send_wq->opcode = opcode;

        if (mask & RXE_END_MASK) {
            qp->send_wq->step_wqe_index();
        }
    }

    return total_send;
}