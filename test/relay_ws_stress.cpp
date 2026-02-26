#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{ false };

enum class AccessMode {
    kRead,
    kWrite,
    kReadWrite,
};

struct Config {
    int threads = 4;
    int cpu_start = 8;
    size_t total_working_set = 64ull * 1024 * 1024;
    size_t stride = 64;
    AccessMode mode = AccessMode::kRead;
    int report_ms = 1000;
    int duration_sec = 0;
    bool lock_memory = false;
    bool use_per_thread_set = false;
    size_t per_thread_set = 0;
};

struct WorkerState {
    std::atomic<uint64_t> touched_bytes{ 0 };
    std::atomic<uint64_t> passes{ 0 };
    std::atomic<uint64_t> sink{ 0 };
};

void on_signal(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

void print_usage(const char *prog) {
    std::fprintf(stdout,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -t, --threads N             Number of worker threads (default: 4)\n"
        "  -c, --cpu-start N           Bind threads to CPU [N, N+threads-1] (default: 8)\n"
        "  -w, --working-set SIZE      Total working set size across all threads (default: 64M)\n"
        "  -p, --per-thread-set SIZE   Working set size per thread (overrides --working-set)\n"
        "  -s, --stride SIZE           Sequential access stride in bytes (default: 64)\n"
        "  -m, --mode MODE             Access mode: read|write|readwrite (default: read)\n"
        "  -r, --report-ms N           Report interval in ms (default: 1000)\n"
        "  -d, --duration N            Duration in seconds, 0 means run until Ctrl+C (default: 0)\n"
        "  -l, --lock-memory           Call mlock() on working set\n"
        "  -h, --help                  Show this help\n"
        "\n"
        "SIZE supports suffix K/M/G/T, e.g., 16M, 2G.\n"
        "Example:\n"
        "  %s --threads 4 --cpu-start 8 --working-set 256M --mode read\n",
        prog, prog);
}

bool parse_size(const char *arg, size_t *out) {
    if (arg == nullptr || *arg == '\0') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long long base = std::strtoull(arg, &end, 10);
    if (errno != 0 || end == arg) {
        return false;
    }

    unsigned long long scale = 1;
    if (*end != '\0') {
        if (*(end + 1) != '\0') {
            return false;
        }

        switch (std::toupper(static_cast<unsigned char>(*end))) {
        case 'K':
            scale = 1ull << 10;
            break;
        case 'M':
            scale = 1ull << 20;
            break;
        case 'G':
            scale = 1ull << 30;
            break;
        case 'T':
            scale = 1ull << 40;
            break;
        default:
            return false;
        }
    }

    if (base > std::numeric_limits<size_t>::max() / scale) {
        return false;
    }

    *out = static_cast<size_t>(base * scale);
    return true;
}

const char *mode_to_string(AccessMode mode) {
    switch (mode) {
    case AccessMode::kRead:
        return "read";
    case AccessMode::kWrite:
        return "write";
    case AccessMode::kReadWrite:
        return "readwrite";
    }
    return "unknown";
}

bool parse_mode(const char *arg, AccessMode *mode) {
    if (arg == nullptr) {
        return false;
    }

    if (std::strcmp(arg, "read") == 0) {
        *mode = AccessMode::kRead;
        return true;
    }
    if (std::strcmp(arg, "write") == 0) {
        *mode = AccessMode::kWrite;
        return true;
    }
    if (std::strcmp(arg, "readwrite") == 0) {
        *mode = AccessMode::kReadWrite;
        return true;
    }
    return false;
}

std::string format_bytes(uint64_t bytes) {
    static const char *kUnits[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = static_cast<double>(bytes);
    size_t unit = 0;
    while (v >= 1024.0 && unit < 4) {
        v /= 1024.0;
        ++unit;
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %s", v, kUnits[unit]);
    return std::string(buf);
}

bool bind_self_to_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0) {
        std::fprintf(stderr, "pthread_setaffinity_np(cpu=%d) failed: %s\n", cpu, std::strerror(rc));
        return false;
    }
    return true;
}

void worker_loop(
    int worker_id,
    int cpu_id,
    uint8_t *buffer,
    size_t buffer_bytes,
    size_t stride,
    AccessMode mode,
    std::atomic<bool> *start_flag,
    WorkerState *state) {
    (void)worker_id;
    bind_self_to_cpu(cpu_id);

    while (!g_stop.load(std::memory_order_relaxed) && !start_flag->load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    uint64_t local_sink = 0;
    const uint8_t xor_mask = 0x5au;

    while (!g_stop.load(std::memory_order_relaxed)) {
        uint64_t touches = 0;
        if (mode == AccessMode::kRead) {
            for (size_t i = 0; i < buffer_bytes; i += stride) {
                local_sink += buffer[i];
                ++touches;
            }
        } else if (mode == AccessMode::kWrite) {
            for (size_t i = 0; i < buffer_bytes; i += stride) {
                buffer[i] ^= xor_mask;
                ++touches;
            }
        } else {
            for (size_t i = 0; i < buffer_bytes; i += stride) {
                buffer[i] ^= xor_mask;
                local_sink += buffer[i];
                ++touches;
            }
        }

        state->touched_bytes.fetch_add(touches * static_cast<uint64_t>(stride), std::memory_order_relaxed);
        state->passes.fetch_add(1, std::memory_order_relaxed);
    }

    state->sink.store(local_sink, std::memory_order_relaxed);
}

bool parse_args(int argc, char **argv, Config *cfg) {
    static struct option long_options[] = {
        { "threads", required_argument, nullptr, 't' },
        { "cpu-start", required_argument, nullptr, 'c' },
        { "working-set", required_argument, nullptr, 'w' },
        { "per-thread-set", required_argument, nullptr, 'p' },
        { "stride", required_argument, nullptr, 's' },
        { "mode", required_argument, nullptr, 'm' },
        { "report-ms", required_argument, nullptr, 'r' },
        { "duration", required_argument, nullptr, 'd' },
        { "lock-memory", no_argument, nullptr, 'l' },
        { "help", no_argument, nullptr, 'h' },
        { nullptr, 0, nullptr, 0 },
    };

    while (true) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "t:c:w:p:s:m:r:d:lh", long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 't': {
            cfg->threads = std::atoi(optarg);
            break;
        }
        case 'c': {
            cfg->cpu_start = std::atoi(optarg);
            break;
        }
        case 'w': {
            size_t v = 0;
            if (!parse_size(optarg, &v)) {
                std::fprintf(stderr, "Invalid --working-set: %s\n", optarg);
                return false;
            }
            cfg->total_working_set = v;
            break;
        }
        case 'p': {
            size_t v = 0;
            if (!parse_size(optarg, &v)) {
                std::fprintf(stderr, "Invalid --per-thread-set: %s\n", optarg);
                return false;
            }
            cfg->use_per_thread_set = true;
            cfg->per_thread_set = v;
            break;
        }
        case 's': {
            size_t v = 0;
            if (!parse_size(optarg, &v)) {
                std::fprintf(stderr, "Invalid --stride: %s\n", optarg);
                return false;
            }
            cfg->stride = v;
            break;
        }
        case 'm': {
            AccessMode mode;
            if (!parse_mode(optarg, &mode)) {
                std::fprintf(stderr, "Invalid --mode: %s\n", optarg);
                return false;
            }
            cfg->mode = mode;
            break;
        }
        case 'r': {
            cfg->report_ms = std::atoi(optarg);
            break;
        }
        case 'd': {
            cfg->duration_sec = std::atoi(optarg);
            break;
        }
        case 'l':
            cfg->lock_memory = true;
            break;
        case 'h':
            print_usage(argv[0]);
            std::exit(0);
        default:
            return false;
        }
    }

    if (cfg->threads <= 0) {
        std::fprintf(stderr, "--threads must be > 0\n");
        return false;
    }
    if (cfg->cpu_start < 0) {
        std::fprintf(stderr, "--cpu-start must be >= 0\n");
        return false;
    }
    if (cfg->stride == 0) {
        std::fprintf(stderr, "--stride must be > 0\n");
        return false;
    }
    if (cfg->report_ms <= 0) {
        std::fprintf(stderr, "--report-ms must be > 0\n");
        return false;
    }
    if (cfg->duration_sec < 0) {
        std::fprintf(stderr, "--duration must be >= 0\n");
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    Config cfg;
    if (!parse_args(argc, argv, &cfg)) {
        print_usage(argv[0]);
        return 1;
    }

    const long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count <= 0) {
        std::fprintf(stderr, "Failed to query CPU count.\n");
        return 1;
    }

    if (cfg.cpu_start + cfg.threads > cpu_count) {
        std::fprintf(stderr,
            "Requested CPUs [%d, %d], but this machine only has %ld CPUs online.\n",
            cfg.cpu_start, cfg.cpu_start + cfg.threads - 1, cpu_count);
        return 1;
    }

    size_t per_thread_bytes = 0;
    if (cfg.use_per_thread_set) {
        per_thread_bytes = cfg.per_thread_set;
    } else {
        if (cfg.total_working_set % static_cast<size_t>(cfg.threads) != 0) {
            std::fprintf(stderr,
                "Total working set (%zu bytes) must be divisible by --threads (%d).\n",
                cfg.total_working_set, cfg.threads);
            return 1;
        }
        per_thread_bytes = cfg.total_working_set / static_cast<size_t>(cfg.threads);
    }

    if (per_thread_bytes == 0) {
        std::fprintf(stderr, "Per-thread working set must be > 0.\n");
        return 1;
    }
    if (per_thread_bytes % cfg.stride != 0) {
        std::fprintf(stderr,
            "Per-thread working set (%zu bytes) must be divisible by stride (%zu bytes).\n",
            per_thread_bytes, cfg.stride);
        return 1;
    }

    const uint64_t total_ws = static_cast<uint64_t>(per_thread_bytes) * static_cast<uint64_t>(cfg.threads);
    std::fprintf(stdout,
        "relay_ws_stress config:\n"
        "  threads            : %d\n"
        "  cpu range          : [%d, %d]\n"
        "  access mode        : %s\n"
        "  stride             : %zu B\n"
        "  per-thread set     : %s\n"
        "  total working set  : %s\n"
        "  report interval    : %d ms\n"
        "  duration           : %d s (0 means until Ctrl+C)\n"
        "  mlock              : %s\n",
        cfg.threads,
        cfg.cpu_start, cfg.cpu_start + cfg.threads - 1,
        mode_to_string(cfg.mode),
        cfg.stride,
        format_bytes(per_thread_bytes).c_str(),
        format_bytes(total_ws).c_str(),
        cfg.report_ms,
        cfg.duration_sec,
        cfg.lock_memory ? "on" : "off");

    std::vector<uint8_t *> buffers(static_cast<size_t>(cfg.threads), nullptr);
    std::vector<WorkerState> worker_states(static_cast<size_t>(cfg.threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(cfg.threads));

    for (int i = 0; i < cfg.threads; ++i) {
        void *ptr = nullptr;
        int rc = posix_memalign(&ptr, 4096, per_thread_bytes);
        if (rc != 0 || ptr == nullptr) {
            std::fprintf(stderr, "posix_memalign failed on worker %d: %s\n", i, std::strerror(rc));
            g_stop.store(true, std::memory_order_relaxed);
            break;
        }

        std::memset(ptr, 1, per_thread_bytes);
        buffers[static_cast<size_t>(i)] = reinterpret_cast<uint8_t *>(ptr);

        if (cfg.lock_memory) {
            if (mlock(ptr, per_thread_bytes) != 0) {
                std::fprintf(stderr, "mlock failed on worker %d: %s\n", i, std::strerror(errno));
            }
        }
    }

    for (int i = 0; i < cfg.threads; ++i) {
        if (buffers[static_cast<size_t>(i)] == nullptr) {
            for (int j = 0; j < cfg.threads; ++j) {
                if (buffers[static_cast<size_t>(j)] != nullptr) {
                    if (cfg.lock_memory) {
                        munlock(buffers[static_cast<size_t>(j)], per_thread_bytes);
                    }
                    std::free(buffers[static_cast<size_t>(j)]);
                }
            }
            std::fprintf(stderr, "Failed to allocate all worker buffers.\n");
            return 1;
        }
    }

    std::atomic<bool> start_flag{ false };
    for (int i = 0; i < cfg.threads; ++i) {
        workers.emplace_back(
            worker_loop,
            i,
            cfg.cpu_start + i,
            buffers[static_cast<size_t>(i)],
            per_thread_bytes,
            cfg.stride,
            cfg.mode,
            &start_flag,
            &worker_states[static_cast<size_t>(i)]);
    }

    const auto begin = std::chrono::steady_clock::now();
    auto last = begin;
    uint64_t last_bytes = 0;

    start_flag.store(true, std::memory_order_release);

    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.report_ms));

        const auto now = std::chrono::steady_clock::now();
        const double elapsed_s = std::chrono::duration<double>(now - begin).count();
        const double delta_s = std::chrono::duration<double>(now - last).count();

        uint64_t total_bytes = 0;
        uint64_t total_passes = 0;
        for (int i = 0; i < cfg.threads; ++i) {
            total_bytes += worker_states[static_cast<size_t>(i)].touched_bytes.load(std::memory_order_relaxed);
            total_passes += worker_states[static_cast<size_t>(i)].passes.load(std::memory_order_relaxed);
        }

        const uint64_t delta_bytes = total_bytes - last_bytes;
        const double bw_gbps = delta_s > 0.0 ? (8.0 * static_cast<double>(delta_bytes) / delta_s / 1e9) : 0.0;

        std::fprintf(stdout,
            "[%8.3f s] stress_bw = %8.3f Gb/s, touched = %s, passes = %" PRIu64 "\n",
            elapsed_s,
            bw_gbps,
            format_bytes(total_bytes).c_str(),
            total_passes);
        std::fflush(stdout);

        last = now;
        last_bytes = total_bytes;

        if (cfg.duration_sec > 0 && elapsed_s >= static_cast<double>(cfg.duration_sec)) {
            g_stop.store(true, std::memory_order_relaxed);
        }
    }

    for (std::thread &t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }

    uint64_t final_sink = 0;
    uint64_t final_bytes = 0;
    for (int i = 0; i < cfg.threads; ++i) {
        final_sink ^= worker_states[static_cast<size_t>(i)].sink.load(std::memory_order_relaxed);
        final_bytes += worker_states[static_cast<size_t>(i)].touched_bytes.load(std::memory_order_relaxed);
    }

    const double total_elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double avg_bw_gbps = total_elapsed_s > 0.0 ? (8.0 * static_cast<double>(final_bytes) / total_elapsed_s / 1e9) : 0.0;

    std::fprintf(stdout,
        "Done. elapsed = %.3f s, avg_stress_bw = %.3f Gb/s, touched = %s, sink = %" PRIu64 "\n",
        total_elapsed_s,
        avg_bw_gbps,
        format_bytes(final_bytes).c_str(),
        final_sink);

    for (int i = 0; i < cfg.threads; ++i) {
        if (buffers[static_cast<size_t>(i)] != nullptr) {
            if (cfg.lock_memory) {
                munlock(buffers[static_cast<size_t>(i)], per_thread_bytes);
            }
            std::free(buffers[static_cast<size_t>(i)]);
        }
    }

    return 0;
}
