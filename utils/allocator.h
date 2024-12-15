#pragma once
#include <iostream>
#include <map>
#include <list>
#include <mutex>
#include <cstddef> // for size_t
#include <cstring> // for memset

class custom_allocator {
public:
    custom_allocator(void *base, size_t size);
    void *alloc(size_t bytes, size_t alignment = 64);
    void free(void *ptr);
    void printFreeList() const;
    void *m_base;  // 内存起始地址
    size_t m_size; // 内存总大小
private:
    std::map<void *, size_t> m_allocMap;
    std::list<std::pair<void *, size_t>> m_freeList;
    mutable std::mutex m_mutex;

    void mergeFreeBlocks();
    void *alignPointer(void *ptr, size_t alignment);
};