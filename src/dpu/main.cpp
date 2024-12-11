#include "smartns.h"
#include "gflags_common.h"
#include "tcp_cm/tcp_cm.h"
#include "rdma_cm/libr.h"

void ctrl_c_handler(int) { stop_flag = true; }

int main(int argc, char *argv[]) {

    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);



    exit(0);
}