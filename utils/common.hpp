#pragma once

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <execinfo.h>
#include <assert.h>
#include <sys/types.h>
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>

#include "log.h"

#define CACHE_LINE_SZ 64

#if defined(__x86_64__)
#include "x86intrin.h"
static double get_tsc_freq_per_ns() {
    // MUST BE CHANGE BY 
    // sudo journalctl -k --grep '^tsc:'  | cut -d' ' -f5-
    return 2.5;
}

static size_t get_tsc() {
    return __rdtsc();
}
#else 
static double get_tsc_freq_per_ns() {
    // sudo dmesg | grep resolution
    return 0.330238342;
}

static size_t get_tsc() {
    size_t tsc;
    asm volatile("mrs %0, cntvct_el0" : "=r" (tsc)); ;
    return tsc;
}
#endif

#define _unused(x) ((void)(x)) // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define Ki(x) (static_cast<size_t>(x) * 1000)
#define Mi(x) (static_cast<size_t>(x) * 1000 * 1000)
#define Gi(x) (static_cast<size_t>(x) * 1000 * 1000 * 1000)

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)

#define min_(x, y) x < y ? x : y

#define max_(x, y) x > y ? x : y

#define min_t(type, x, y) ({            \
    type __min1 = (x);            \
    type __min2 = (y);            \
    __min1 < __min2 ? __min1: __min2; })

#define max_t(type, x, y) ({            \
    type __max1 = (x);            \
    type __max2 = (y);            \
    __max1 > __max2 ? __max1: __max2; })

#define SWAP(x, y)             \
    do                         \
    {                          \
        typeof(x) ____val = x; \
        x = y;                 \
        y = ____val;           \
    } while (0)

#define is_log2(v) (((v) & ((v)-1)) == 0)

#define Assert(condition) SmartNS::rt_assert(condition, __FILE__, __LINE__);

static size_t round_up(size_t num, size_t factor) {
    return num + factor - 1 - ((num + factor - 1) % factor);
}

namespace SmartNS {

#pragma GCC diagnostic ignored "-Wunused-function"



    static void print_bt() {
        void *array[10];
        size_t size;

        // get void*'s for all entries on the stack
        size = static_cast<size_t>(backtrace(array, 10));

        // print out all the frames to stderr
        backtrace_symbols_fd(array, size, STDERR_FILENO);
    }

    static inline void rt_assert(bool condition, const std::string &throw_str, char *s) {
        if (unlikely(!condition)) {
            print_bt();
            throw std::runtime_error(throw_str + std::string(s));
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const char *throw_str) {
        if (unlikely(!condition)) {
            print_bt();
            throw std::runtime_error(std::string(throw_str));
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const std::string &throw_str) {
        if (unlikely(!condition)) {
            print_bt();
            throw std::runtime_error(throw_str);
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    /// This is faster than rt_assert(cond, str) as it avoids string construction.
    static inline void rt_assert(bool condition) {
        if (unlikely(!condition)) {
            print_bt();
            throw std::runtime_error("Error");
        }
    }

    static inline void rt_assert(bool condition, const char *file_name, int line) {
        if (unlikely(!condition)) {
            print_bt();
            throw std::runtime_error(std::string(file_name) + ":" + std::to_string(line));
        }
    }

    template <typename... Args>
    std::string string_format(const std::string &format, Args... args) {
        int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1; // Extra space for '\0'
        if (size_s <= 0) {
            throw std::runtime_error("Error during formatting.");
        }
        auto size = static_cast<size_t>(size_s);
        std::unique_ptr<char[]> buf(new char[size]);
        std::snprintf(buf.get(), size, format.c_str(), args...);
        return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
    }

}
