#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "numautil.h"
#include "hdr_histogram.h"
#include <random>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_map>

// client: ./linked_list_rdma -num_queries 65535 -deviceName mlx5_0 -serverIp 10.0.0.200
// server: ./linked_list_rdma -num_queries 65535 -deviceName mlx5_0

// Adding list size parameter
DEFINE_uint64(num_queries, 65535, "Number of queries to perform");
DEFINE_uint64(hops, 99, "Number of hops to perform");

// Helper function to determine if we're running as server
inline bool is_server() {
    return FLAGS_serverIp.empty();
}

struct Node {
    int key;
    struct Node *next;
    void *value;       // Keep as pointer to separate memory
    char padding[108]; // Adjusted to make each node 128 bytes (two cache lines)
};

static constexpr size_t VALUE_SIZE = 64;
static constexpr size_t LLC_SIZE = 75 * 1024 * 1024; // 75MB
static constexpr size_t NUM_NODES = LLC_SIZE / sizeof(Node);

struct ListInfo {
    Node *head;
    size_t list_size;
};

std::atomic<bool> stop_flag = false;
size_t base_alloc_size;

void ctrl_c_handler(int) { stop_flag = true; }

void initialize_linked_list(Node *nodes, size_t list_size) {
    /**
     * Linked List Memory Layout:
     *
     * [Node -1] [Node 1] ... [Node N-1]
     * [Value -1] [Value 1] ... [Value N-1]
     * [ListInfo]
     *
     */
    std::vector<int> keys(list_size);
    for (size_t i = 0; i < list_size; i++) {
        keys[i] = i;
    }

    // Calculate where values memory starts (after all nodes)
    char *values_memory = reinterpret_cast<char *>(nodes + list_size);

    // Create linked list
    for (size_t i = 0; i < list_size; i++) {
        nodes[i].key = keys[i];

        // Point to separate memory location for value (64 bytes each)
        nodes[i].value = values_memory + (i * VALUE_SIZE);

        // Initialize the value buffer with some data
        memset(nodes[i].value, keys[i] & 0xFF, VALUE_SIZE);

        // 0 -> NUM_NODES - 1 -> 1 -> NUM_NODES - 2 -> 2 -> NUM_NODES - 3 -> ... -> 0
        if (i < list_size / 2) {
            nodes[i].next = &nodes[list_size - 1 - i];
        } else if (i == list_size / 2) {
            nodes[i].next = nodes;
        } else {
            nodes[i].next = &nodes[list_size - i];
        }
    }

    printf("Linked list initialized with %zu nodes, each node size: %zu bytes\n"
        "Values stored in separate memory starting at %p\n",
        list_size, sizeof(Node), values_memory);
}

/**
 * @brief
 *
 * @param cq
 * @param wc
 * @param op_name
 * @return int 0 on success, 1 on failure
 */
int rdma_poll_completion(struct ibv_cq *cq, struct ibv_wc *wc, const char *op_name) {
    int ne;

    do {
        ne = ibv_poll_cq(cq, 1, wc);
    } while (ne == 0);

    if (ne < 0) {
        fprintf(stderr, "Failed to poll CQ\n");
        return 1;
    }

    if (wc->status != IBV_WC_SUCCESS) {
        fprintf(stderr, "RDMA %s failed with status %d (%s)\n",
            op_name, wc->status, ibv_wc_status_str(wc->status));
        return 1;
    }

    return 0;
}

int rdma_read_sync(qp_handler &handler, void *local_buf, uint64_t remote_addr, size_t size) {
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_sr = NULL;
    struct ibv_wc wc;

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = IBV_WR_RDMA_READ;
    sr.send_flags = IBV_SEND_SIGNALED;
    sr.wr.rdma.remote_addr = remote_addr;
    sr.wr.rdma.rkey = handler.remote_rkey;

    sge.addr = (uintptr_t)local_buf;
    sge.length = size;
    sge.lkey = handler.mr->lkey;

    if (ibv_post_send(handler.qp, &sr, &bad_sr)) {
        fprintf(stderr, "Failed to post SR for RDMA read\n");
        return 1;
    }

    return rdma_poll_completion(handler.send_cq, &wc, "read");
}

// Add new synchronization functions
int rdma_send_sync(qp_handler &handler, void *local_buf, size_t size) {
    struct ibv_send_wr sr;
    struct ibv_sge sge;
    struct ibv_send_wr *bad_sr = NULL;
    struct ibv_wc wc;

    memset(&sr, 0, sizeof(sr));
    sr.next = NULL;
    sr.wr_id = 0;
    sr.sg_list = &sge;
    sr.num_sge = 1;
    sr.opcode = IBV_WR_SEND;
    sr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uintptr_t)local_buf;
    sge.length = size;
    sge.lkey = handler.mr->lkey;

    if (ibv_post_send(handler.qp, &sr, &bad_sr)) {
        fprintf(stderr, "Failed to post SR for RDMA send\n");
        return 1;
    }

    return rdma_poll_completion(handler.send_cq, &wc, "send");
}

int rdma_recv_sync(qp_handler &handler, void *local_buf, size_t size) {
    struct ibv_recv_wr rr;
    struct ibv_sge sge;
    struct ibv_recv_wr *bad_rr = NULL;
    struct ibv_wc wc;

    memset(&rr, 0, sizeof(rr));
    rr.next = NULL;
    rr.wr_id = 0;
    rr.sg_list = &sge;
    rr.num_sge = 1;

    sge.addr = (uintptr_t)local_buf;
    sge.length = size;
    sge.lkey = handler.mr->lkey;

    if (ibv_post_recv(handler.qp, &rr, &bad_rr)) {
        fprintf(stderr, "Failed to post RR for RDMA receive\n");
        return 1;
    }

    return rdma_poll_completion(handler.recv_cq, &wc, "receive");
}

// Modify sub_task_server to send the ListInfo directly
void sub_task_server(qp_handler *handler) {
    // wait_scheduling(0, 0);

    // Initialize linked list in server thread
    Node *nodes = reinterpret_cast<Node *>(handler->buf);
    initialize_linked_list(nodes, NUM_NODES);

    // Create list info to communicate head pointer
    ListInfo *list_info = reinterpret_cast<ListInfo *>(
        reinterpret_cast<char *>(handler->buf) + base_alloc_size - sizeof(ListInfo));
    list_info->head = nodes;
    list_info->list_size = NUM_NODES;

    printf("Server: List initialized with head at %p\n", list_info->head);

    if (rdma_send_sync(*handler, list_info, sizeof(ListInfo)) != 0) {
        fprintf(stderr, "Failed to send ListInfo\n");
        return;
    }

    printf("Server: Sent ListInfo to client\n");

    while (!stop_flag) {
        sleep(1);
    }
}

// Modify sub_task_client to receive ListInfo directly
void sub_task_client(qp_handler *handler) {
    // wait_scheduling(0, 0);

    ListInfo *list_info = reinterpret_cast<ListInfo *>(
        reinterpret_cast<char *>(handler->buf) + base_alloc_size - sizeof(ListInfo));
    printf("Client: Waiting for server to send ListInfo...\n");

    if (rdma_recv_sync(*handler, list_info, sizeof(ListInfo))) {
        fprintf(stderr, "Failed to receive ListInfo\n");
        return;
    }

    printf("Client received list info: head=%p, list_size=%zu\n",
        list_info->head, list_info->list_size);

    // Allocate buffer for storing nodes during traversal
    Node *local_node = reinterpret_cast<Node *>(handler->buf);
    void *local_value = reinterpret_cast<void *>(local_node + 1);

    // Initialize histogram for latency measurements
    struct hdr_histogram *histogram;
    hdr_init(1, 1000000, 3, &histogram); // 1us to 1s range with 3 significant digits

    // Add tracking for hops and RDMA access time
    uint64_t total_hops = 0;
    uint64_t total_rdma_time_us = 0;
    struct hdr_histogram *rdma_histogram;
    hdr_init(1, 1000000, 3, &rdma_histogram);

    Node *current = list_info->head;
    int target_key = 0;

    // Perform random key lookups
    for (size_t i = 0; i < FLAGS_num_queries && !stop_flag; i++) {
        target_key += FLAGS_hops;
        target_key %= NUM_NODES / 2;
        int hops = 0;
        uint64_t query_rdma_time_us = 0;

        uint64_t start_time = get_tsc();

        // Traverse the linked list using RDMA reads
        while (current != nullptr) {
            uint64_t rdma_start = get_tsc();
            if (rdma_read_sync(*handler, local_node, (uint64_t)current, sizeof(Node)) != 0) {
                fprintf(stderr, "Failed to read node at %p\n", current);
                break;
            }
            uint64_t rdma_end = get_tsc();
            uint64_t rdma_duration = rdma_end - rdma_start;
            query_rdma_time_us += rdma_duration;
            hdr_record_value(rdma_histogram, rdma_duration * 10);

            hops++;

            if (local_node->key == target_key) {
                // Found the target key, fetch value
                rdma_start = get_tsc();
                if (rdma_read_sync(*handler, local_value, (uint64_t)local_node->value, VALUE_SIZE) != 0) {
                    fprintf(stderr, "Failed to read value at %p\n", local_node->value);
                    break;
                }
                rdma_end = get_tsc();
                rdma_duration = rdma_end - rdma_start;
                query_rdma_time_us += rdma_duration;
                hdr_record_value(rdma_histogram, rdma_duration * 10);
                break;
            }

            current = local_node->next;
        }

        uint64_t end_time = get_tsc();
        uint64_t duration = end_time - start_time;

        // Convert to appropriate units for recording
        hdr_record_value(histogram, duration * 10);

        // Update totals for averages
        total_hops += hops;
        total_rdma_time_us += query_rdma_time_us;

        if (i % 10000 == 0) {
            printf("Query %zu: Found key %d after %d hops in %lu tsc units (RDMA time: %lu tsc units)\n",
                i, target_key, hops, duration, query_rdma_time_us);
        }
    }

    // Calculate and print averages
    double avg_hops = FLAGS_num_queries > 0 ? (double)total_hops / FLAGS_num_queries : 0;
    double avg_rdma_time = FLAGS_num_queries > 0 ? (double)total_rdma_time_us / FLAGS_num_queries : 0;
    double ns_per_cycle = 1.0 / get_tsc_freq_per_ns();

    printf("\nAverage Statistics:\n");
    printf("Average hops per query: %.2f\n", avg_hops);
    printf("Average RDMA operation time: %.2f tsc units (%.2f ns)\n",
        avg_rdma_time, avg_rdma_time * ns_per_cycle);
    printf("Average query time: %.2f tsc units (%.2f ns)\n",
        hdr_mean(histogram) / 10.0, (hdr_mean(histogram) / 10.0) * ns_per_cycle);

    printf("\nLatency statistics:\n");
    printf("Min: %.2f ns\n", (hdr_min(histogram) / 10.0) * ns_per_cycle);
    printf("Mean: %.2f ns\n", (hdr_mean(histogram) / 10.0) * ns_per_cycle);
    printf("Median: %.2f ns\n", (hdr_value_at_percentile(histogram, 50.0) / 10.0) * ns_per_cycle);
    printf("90th percentile: %.2f ns\n", (hdr_value_at_percentile(histogram, 90.0) / 10.0) * ns_per_cycle);
    printf("99th percentile: %.2f ns\n", (hdr_value_at_percentile(histogram, 99.0) / 10.0) * ns_per_cycle);
    printf("Max: %.2f ns\n", (hdr_max(histogram) / 10.0) * ns_per_cycle);

    printf("\nRDMA operation latency:\n");
    printf("Min: %.2f ns\n", (hdr_min(rdma_histogram) / 10.0) * ns_per_cycle);
    printf("Mean: %.2f ns\n", (hdr_mean(rdma_histogram) / 10.0) * ns_per_cycle);
    printf("Median: %.2f ns\n", (hdr_value_at_percentile(rdma_histogram, 50.0) / 10.0) * ns_per_cycle);
    printf("99th percentile: %.2f ns\n", (hdr_value_at_percentile(rdma_histogram, 99.0) / 10.0) * ns_per_cycle);

    // Print detailed percentiles using the same approach as test_pipe.cpp
    hdr_percentiles_print(histogram, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);
    hdr_percentiles_print(rdma_histogram, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);

    hdr_close(histogram);
    hdr_close(rdma_histogram);

    sleep(1);
}

void benchmark() {
    tcp_param net_param;
    net_param.isServer = is_server();
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;
    socket_init(net_param);

    rdma_param rdma_param;
    rdma_param.device_name = FLAGS_deviceName;
    rdma_param.numa_node = FLAGS_numaNode;
    rdma_param.batch_size = FLAGS_batch_size;
    rdma_param.sge_per_wr = 1;
    roce_init(rdma_param, 1); // Set to 1 thread

    pingpong_info info[2]; // Just need 2 info structs for a single connection
    memset(info, 0, sizeof(info));

    base_alloc_size = NUM_NODES * (sizeof(Node) + VALUE_SIZE) + sizeof(ListInfo);
    void *buf = get_huge_mem(FLAGS_numaNode, base_alloc_size);
    memset(buf, 0, base_alloc_size);

    qp_handler *handler = create_qp_rc(rdma_param, buf, base_alloc_size, &info[0], 0);

    exchange_data(net_param, reinterpret_cast<char *>(&info[0]),
        reinterpret_cast<char *>(&info[1]),
        sizeof(pingpong_info));

    connect_qp_rc(rdma_param, *handler, &info[1], &info[0]);

    // Execute server or client task directly in the main thread
    if (is_server()) {
        sub_task_server(handler);
    } else {
        sub_task_client(handler);
    }

    // Cleanup
    free(handler->send_sge_list);
    free(handler->recv_sge_list);
    free(handler->send_wr);
    free(handler->recv_wr);

    ibv_destroy_qp(handler->qp);
    ibv_destroy_cq(handler->send_cq);
    ibv_destroy_cq(handler->recv_cq);
    ibv_dereg_mr(handler->mr);
    ibv_dealloc_pd(handler->pd);
    ibv_close_device(rdma_param.contexts[0]);

    free_huge_mem(reinterpret_cast<void *>(handler->buf));
    free(handler);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    benchmark();

    return 0;
}