#include "smartns_dv.h"
#include "rdma_cm/libsmartns.h"
#include "tcp_cm/tcp_cm.h"
#include "gflags_common.h"
#include "numautil.h"
#include "hdr_histogram.h"
#include "devx/devx_device.h"
#include <random>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_map>

// client: ./linked_list_relay -num_queries 65535 -deviceName mlx5_0 -serverIp 10.0.0.200 -relayIp 10.0.0.201
// relay:  ./linked_list_relay -num_queries 65535 -deviceName mlx5_2 -serverIp 10.0.0.200 -is_relay
// server: ./linked_list_relay -num_queries 65535 -deviceName mlx5_0

// Adding list size parameter
DEFINE_uint64(num_queries, 65535, "Number of queries to perform");
DEFINE_uint64(hops, 99, "Number of hops to perform");
DEFINE_bool(is_relay, false, "Run as relay");
DEFINE_string(relayIp, "", "IP address of relay node");

// Helper functions to determine roles
inline bool is_server() {
    return FLAGS_serverIp.empty() && !FLAGS_is_relay;
}

inline bool is_relay() {
    return FLAGS_is_relay;
}

inline bool is_client() {
    return !FLAGS_serverIp.empty() && !FLAGS_is_relay;
}

static constexpr size_t VALUE_SIZE = 64;

struct Node {
    struct Node *next;
    void *value;       // Keep as pointer to separate memory
    int key;
    char padding[108]; // Adjusted to make each node 128 bytes (two cache lines)
};

static constexpr size_t LLC_SIZE = 75 * 1024 * 1024; // 75MB
static constexpr size_t NUM_NODES = LLC_SIZE / sizeof(Node);

// Query and result structures for relay communication
struct Query {
    int target_key;
};

struct QueryResult {
    int key;
    char value[VALUE_SIZE];
    int hops;
    bool found;
};

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

// Add DMA memcpy function for relay
int dma_memcpy_sync(ibv_qp_ex *qpx, mlx5dv_qp_ex *mqpx, uint32_t local_mkey, void *local_addr,
    uint32_t remote_mkey, uint64_t remote_addr, size_t size, ibv_cq *cq) {
    struct ibv_wc wc;

    qpx->wr_id = 0;
    qpx->wr_flags = IBV_SEND_SIGNALED;

    mqpx->wr_memcpy_direct(mqpx, local_mkey, (uint64_t)local_addr, remote_mkey, remote_addr, size);

    int ne;
    do {
        ne = ibv_poll_cq(cq, 1, &wc);
    } while (ne == 0);

    if (ne < 0) {
        fprintf(stderr, "Failed to poll CQ for DMA\n");
        return 1;
    }

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "DMA operation failed with status %d (%s)\n", wc.status, ibv_wc_status_str(wc.status));
        return 1;
    }

    return 0;
}

// Add relay task function
void sub_task_relay(qp_handler *client_handler, vhca_resource *server_resource) {
    wait_scheduling(0, 1);

    // Initialize DMA components for server memory access
    ibv_mr *local_mr = ibv_reg_mr(server_resource->pd, (void *)client_handler->buf, base_alloc_size,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
        IBV_ACCESS_HUGETLB | IBV_ACCESS_RELAXED_ORDERING);
    if (!local_mr) {
        fprintf(stderr, "Failed to register MR for relay\n");
        return;
    }

    // Create DMA queue pairs
    ibv_cq *sq_cq = create_dma_cq(server_resource->pd->context, 128);
    ibv_cq *rq_cq = create_dma_cq(server_resource->pd->context, 128);
    ibv_qp *dma_qp = create_dma_qp(server_resource->pd->context, server_resource->pd, rq_cq, sq_cq, 128);
    init_dma_qp(dma_qp);
    dma_qp_self_connected(dma_qp);
    ibv_qp_ex *dma_qpx = ibv_qp_to_qp_ex(dma_qp);
    mlx5dv_qp_ex *dma_mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(dma_qpx);
    dma_mqpx->wr_memcpy_direct_init(dma_mqpx);

    uint32_t local_mr_mkey = local_mr->lkey;
    uint32_t remote_mr_mkey = devx_mr_query_mkey(server_resource->mr);

    // Receive ListInfo from server
    ListInfo *list_info = reinterpret_cast<ListInfo *>(
        reinterpret_cast<char *>(client_handler->buf) + base_alloc_size - sizeof(ListInfo));

    // DMA copy ListInfo to local memory
    // sleep to wait for server list initialization
    sleep(2);
    dma_memcpy_sync(dma_qpx, dma_mqpx, local_mr_mkey, list_info,
        remote_mr_mkey, (uint64_t)server_resource->addr + base_alloc_size - sizeof(ListInfo),
        sizeof(ListInfo), sq_cq);

    printf("Relay received list info: head=%p, list_size=%zu\n",
        list_info->head, list_info->list_size);

    // Buffer for local copies of nodes and values
    Node *local_node = reinterpret_cast<Node *>(client_handler->buf);
    void *local_value_buf = reinterpret_cast<void *>(local_node + 1);

    // Buffer for client queries and results
    Query *query = reinterpret_cast<Query *>(
        reinterpret_cast<char *>(client_handler->buf) + 2 * sizeof(Node) + VALUE_SIZE);
    QueryResult *result = reinterpret_cast<QueryResult *>(query + 1);

    // Add histogram for DMA operation latency
    struct hdr_histogram *dma_histogram;
    hdr_init(1000, 50000000, 3, &dma_histogram); // 1ms to 50s range with 3 significant digits

    // Track total DMA time
    uint64_t total_dma_time = 0;
    uint64_t dma_operations = 0;

    Node *current = list_info->head;
    // Process client queries
    while (!stop_flag) {
        // Wait for query from client
        if (rdma_recv_sync(*client_handler, query, sizeof(Query))) {
            fprintf(stderr, "Failed to receive query from client\n");
            break;
        }

        if (query->target_key < 0) {
            // Termination signal
            printf("Relay: Received termination signal\n");
            break;
        }

        int target_key = query->target_key;
        int hops = 0;
        bool found = false;
        uint64_t query_dma_time = 0;

        // DMA traverse the linked list
        while (current != nullptr) {
            // Copy node from server memory
            size_t dma_start = get_tsc();
            if (dma_memcpy_sync(dma_qpx, dma_mqpx, local_mr_mkey, local_node,
                remote_mr_mkey, (uint64_t)current, sizeof(Node), sq_cq) != 0) {
                fprintf(stderr, "Failed to read node at %p\n", current);
                break;
            }
            size_t dma_end = get_tsc();
            size_t dma_duration = dma_end - dma_start;
            query_dma_time += dma_duration;
            hdr_record_value(dma_histogram, dma_duration * 10); // *10 to convert to ns when printing
            dma_operations++;

            hops++;

            if (local_node->key == target_key) {
                // Found the target key, fetch value using DMA
                dma_start = get_tsc();
                if (dma_memcpy_sync(dma_qpx, dma_mqpx, local_mr_mkey, local_value_buf,
                    remote_mr_mkey, (uint64_t)local_node->value, VALUE_SIZE, sq_cq) != 0) {
                    fprintf(stderr, "Failed to read value at %p\n", local_node->value);
                    break;
                }
                dma_end = get_tsc();
                dma_duration = dma_end - dma_start;
                query_dma_time += dma_duration;
                hdr_record_value(dma_histogram, dma_duration * 10);
                dma_operations++;

                found = true;
                break;
            }

            current = local_node->next;
        }

        total_dma_time += query_dma_time;

        // Prepare result
        result->key = target_key;
        result->hops = hops;
        result->found = found;
        if (found) {
            memcpy(result->value, local_value_buf, VALUE_SIZE);
        } else {
            memset(result->value, 0, VALUE_SIZE);
        }

        // Send result back to client
        if (rdma_send_sync(*client_handler, result, sizeof(QueryResult)) != 0) {
            fprintf(stderr, "Failed to send result to client\n");
            break;
        }
    }

    // Print DMA operation statistics
    double avg_dma_time = dma_operations > 0 ? (double)total_dma_time / dma_operations : 0;

    printf("\nDMA operation statistics:\n");
    printf("Total DMA operations: %lu\n", dma_operations);
    printf("Average DMA operation time: %.2f cycles (%.2f ns)\n",
        avg_dma_time, avg_dma_time / get_tsc_freq_per_ns());

    printf("\nDMA operation latency:\n");
    hdr_percentiles_print(dma_histogram, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);

    // Free the histogram
    hdr_close(dma_histogram);

    // Clean up
    ibv_destroy_qp(dma_qp);
    ibv_destroy_cq(sq_cq);
    ibv_destroy_cq(rq_cq);
    ibv_dereg_mr(local_mr);

    printf("Relay task completed\n");
}

// Modify sub_task_server to share ListInfo with relay
void sub_task_server(qp_handler *handler) {
    wait_scheduling(0, 1);

    // Initialize linked list in server thread
    Node *nodes = reinterpret_cast<Node *>(handler->buf);
    initialize_linked_list(nodes, NUM_NODES);

    // Create list info to let relay access the list
    ListInfo *list_info = reinterpret_cast<ListInfo *>(
        reinterpret_cast<char *>(handler->buf) + base_alloc_size - sizeof(ListInfo));
    list_info->head = nodes;
    list_info->list_size = NUM_NODES;

    printf("Server: List initialized with head at %p\n", nodes);

    while (!stop_flag) {
        sleep(1);
    }
}

// Modify sub_task_client to send queries to relay and receive results
void sub_task_client(qp_handler *handler) {
    wait_scheduling(0, 1);

    // Buffer for sending queries and receiving results
    Query *query = reinterpret_cast<Query *>(handler->buf);
    QueryResult *result = reinterpret_cast<QueryResult *>(query + 1);

    // Initialize histogram for latency measurements
    struct hdr_histogram *histogram;
    hdr_init(1000, 50000000, 3, &histogram); // 1ms to 50s range with 3 significant digits

    // Add tracking for relay query time
    uint64_t total_hops = 0;
    uint64_t total_query_time = 0;
    int target_key = 0;
    sleep(5);

    // Perform random key lookups
    for (size_t i = 0; i < FLAGS_num_queries && !stop_flag; i++) {
        target_key += FLAGS_hops;
        target_key %= NUM_NODES / 2;
        query->target_key = target_key;

        size_t start_time = get_tsc();

        // Send query to relay
        if (rdma_send_sync(*handler, query, sizeof(Query)) != 0) {
            fprintf(stderr, "Failed to send query to relay\n");
            break;
        }

        // Receive result from relay
        if (rdma_recv_sync(*handler, result, sizeof(QueryResult)) != 0) {
            fprintf(stderr, "Failed to receive result from relay\n");
            break;
        }

        size_t end_time = get_tsc();
        size_t duration = end_time - start_time;

        // Record latency
        hdr_record_value(histogram, duration * 10); // *10 to convert to ns when printing

        // Update statistics
        total_hops += result->hops;
        total_query_time += duration;

        // if (i % 10000 == 0) {
        //     printf("Query %zu: Key %d %s after %d hops in %.2f us\n",
        //         i, target_key, result->found ? "found" : "not found",
        //         result->hops, duration * get_tsc_freq_per_ns() / 1000.0);
        // }
    }

    // Send termination signal
    query->target_key = -1;
    rdma_send_sync(*handler, query, sizeof(Query));

    // Calculate and print averages
    double avg_hops = FLAGS_num_queries > 0 ? (double)total_hops / FLAGS_num_queries : 0;
    double avg_query_time = FLAGS_num_queries > 0 ? (double)total_query_time / FLAGS_num_queries : 0;

    printf("\nAverage Statistics:\n");
    printf("Average hops per query: %.2f\n", avg_hops);
    printf("Average query time: %.2f cycles (%.2f us)\n",
        avg_query_time, avg_query_time / get_tsc_freq_per_ns() / 1000.0);

    printf("\nLatency statistics:\n");
    hdr_percentiles_print(histogram, stdout, 5, 10 * get_tsc_freq_per_ns(), CLASSIC);

    hdr_close(histogram);
    sleep(1);
}

// Add function for DMA connection between relay and server
vhca_resource *connect_peer_dma(tcp_param &net_param, rdma_param &rdma_param, void **bufs) {
    size_t dummy;
    (void)dummy;

    vhca_resource *resources = new vhca_resource[1]; // Just need one for this application
    roce_init(rdma_param, 1);
    socket_init(net_param);

    if (is_server()) {
        // Server exports memory for relay to access
        struct devx_hca_capabilities caps;
        if (devx_query_hca_caps(rdma_param.contexts[0], &caps) != 0) {
            throw std::runtime_error("can't query_hca_caps");
        }

        printf("Server: vhca_id %u\n", caps.vhca_id);
        resources[0].pd = ibv_alloc_pd(rdma_param.contexts[0]);

        uint8_t access_key[32] = { 0 };
        for (size_t i = 0; i < 32; i++) {
            access_key[i] = 1;
        }

        resources[0].vhca_id = caps.vhca_id;
        resources[0].addr = bufs[0];
        resources[0].size = base_alloc_size;
        resources[0].mr = devx_reg_mr(resources[0].pd, resources[0].addr, resources[0].size,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
            IBV_ACCESS_REMOTE_WRITE);
        if (!resources[0].mr) {
            throw std::runtime_error("can't devx_reg_mr");
        }

        resources[0].mkey = devx_mr_query_mkey(resources[0].mr);
        if (devx_mr_allow_other_vhca_access(resources[0].mr, access_key, sizeof(access_key)) != 0) {
            throw std::runtime_error("can't allow_other_vhca_access");
        }

        printf("Server: mr (umem): vhca_id %u addr %p mkey %u\n",
            caps.vhca_id, resources[0].addr, resources[0].mkey);

        // Send resource info to relay
        dummy = write(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource));
    } else if (is_relay()) {
        // Relay imports memory from server
        dummy = read(net_param.connfd, reinterpret_cast<char *>(resources), sizeof(vhca_resource));

        uint32_t mmo_dma_max_length = get_mmo_dma_max_length(rdma_param.contexts[0]);
        fprintf(stderr, "Relay: mmo_dma_max_length %u\n", mmo_dma_max_length);
        rt_assert(mmo_dma_max_length >= sizeof(Node));

        uint8_t access_key[32] = { 0 };
        for (int i = 0; i < 32; i++) {
            access_key[i] = 1;
        }

        resources[0].pd = ibv_alloc_pd(rdma_param.contexts[0]);
        if (resources[0].pd == nullptr) {
            throw std::runtime_error("ibv_alloc_pd failed");
        }

        resources[0].mr = devx_create_crossing_mr(resources[0].pd, resources[0].addr,
            resources[0].size, resources[0].vhca_id,
            resources[0].mkey, access_key, sizeof(access_key));
        if (resources[0].mr == nullptr) {
            throw std::runtime_error("devx_create_crossing_mr failed");
        }

        printf("Relay: Imported server memory: vhca_id %u addr %p mkey %u\n",
            resources[0].vhca_id, resources[0].addr, resources[0].mkey);
    }

    return resources;
}

void benchmark() {
    tcp_param net_param;
    net_param.isServer = is_server();
    net_param.serverIp = FLAGS_serverIp;
    net_param.sock_port = FLAGS_port;

    rdma_param rdma_param;
    rdma_param.device_name = FLAGS_deviceName;
    rdma_param.numa_node = FLAGS_numaNode;
    rdma_param.batch_size = FLAGS_batch_size;
    rdma_param.sge_per_wr = 1;

    base_alloc_size = NUM_NODES * (sizeof(Node) + VALUE_SIZE) + sizeof(ListInfo) +
        sizeof(Query) + sizeof(QueryResult);

    void *bufs[1]; // Just need one buffer for this application
    bufs[0] = get_huge_mem(FLAGS_numaNode, base_alloc_size);
    memset(bufs[0], 0, base_alloc_size);

    qp_handler *handler = nullptr;
    vhca_resource *dma_resource = nullptr;

    std::thread thread;
    // Setup connections based on role
    if (is_server()) {
        // Setup new TCP connection for DMA resources
        net_param.sock_port = FLAGS_port + 1;
        net_param.isServer = true;
        dma_resource = connect_peer_dma(net_param, rdma_param, bufs);

        // handler is only used for passing buffer to sub_task_server
        handler = (qp_handler *)malloc(sizeof(qp_handler));
        handler->buf = (size_t)bufs[0];

        thread = std::thread(sub_task_server, handler);
        bind_to_core(thread, 0, 1);
    } else if (is_relay()) {
        // Setup DMA access to server memory
        net_param.sock_port = FLAGS_port + 1;
        net_param.isServer = false;
        dma_resource = connect_peer_dma(net_param, rdma_param, bufs);

        // Connect to client via RDMA
        net_param.sock_port = FLAGS_port + 2;
        net_param.isServer = true;
        net_param.serverIp = "";
        socket_init(net_param);

        qp_handler *client_handler;
        pingpong_info info2[2];
        memset(info2, 0, sizeof(info2));

        client_handler = create_qp_rc(rdma_param, bufs[0], base_alloc_size, &info2[0], 0);

        exchange_data(net_param, reinterpret_cast<char *>(&info2[0]),
            reinterpret_cast<char *>(&info2[1]),
            sizeof(pingpong_info));

        connect_qp_rc(rdma_param, *client_handler, &info2[1], &info2[0]);

        thread = std::thread(sub_task_relay, client_handler, dma_resource);
        bind_to_core(thread, 0, 1);
    } else {
        // Client connects to relay via RDMA
        net_param.serverIp = FLAGS_relayIp;
        net_param.sock_port = FLAGS_port + 2;
        roce_init(rdma_param, 1);
        socket_init(net_param);

        pingpong_info info[2];
        memset(info, 0, sizeof(info));

        handler = create_qp_rc(rdma_param, bufs[0], base_alloc_size, &info[0], 0);

        exchange_data(net_param, reinterpret_cast<char *>(&info[0]),
            reinterpret_cast<char *>(&info[1]),
            sizeof(pingpong_info));

        connect_qp_rc(rdma_param, *handler, &info[1], &info[0]);

        thread = std::thread(sub_task_client, handler);
        bind_to_core(thread, 0, 1);
    }

    thread.join();

    // Cleanup
    if (handler) {
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

        free(handler);
    }

    if (dma_resource) {
        delete[] dma_resource;
    }

    free_huge_mem(bufs[0]);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    benchmark();

    return 0;
}