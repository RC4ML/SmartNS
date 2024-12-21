#pragma once

#include "smartns.h"

int rxe_handle_req(datapath_handler *handler, dpu_qp *qp);

int rxe_handle_recv(datapath_handler *handler);