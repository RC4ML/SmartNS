#include "allocator.h"
custom_allocator::custom_allocator(void *base, size_t size)
    : m_base(base), m_size(size) {
    // 将整个内存视为一个初始空闲块
    m_freeList.emplace_back(base, size);
}

// 分配内存
void *custom_allocator::alloc(size_t bytes, size_t alignment) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_freeList.begin(); it != m_freeList.end(); ++it) {
        void *alignedPtr = alignPointer(it->first, alignment);
        size_t padding = static_cast<char *>(alignedPtr) - static_cast<char *>(it->first);
        size_t totalSize = bytes + padding;

        if (it->second >= totalSize) {
            void *allocated = alignedPtr;

            // 更新空闲块
            if (it->second > totalSize) {
                it->first = static_cast<char *>(it->first) + totalSize;
                it->second -= totalSize;
            } else {
                m_freeList.erase(it);
            }

            // 记录分配信息
            m_allocMap[allocated] = bytes;
            return allocated;
        }
    }

    // 内存不足
    return nullptr;
}

// 释放内存
void custom_allocator::free(void *ptr) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_allocMap.find(ptr);
    if (it != m_allocMap.end()) {
        size_t size = it->second;

        // 从分配表中移除
        m_allocMap.erase(it);

        // 将释放的块加入空闲链表
        m_freeList.emplace_back(ptr, size);

        // 合并相邻块以减少碎片
        mergeFreeBlocks();
    }
}

// 对齐指针
void *custom_allocator::alignPointer(void *ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void *>(alignedAddr);
}

// 合并相邻的空闲块
void custom_allocator::mergeFreeBlocks() {
    m_freeList.sort([](const auto &a, const auto &b) {
        return a.first < b.first;
        });

    auto it = m_freeList.begin();
    while (it != m_freeList.end()) {
        auto next = std::next(it);
        if (next != m_freeList.end() &&
            static_cast<char *>(it->first) + it->second == next->first) {
            // 合并两个相邻块
            it->second += next->second;
            m_freeList.erase(next);
        } else {
            ++it;
        }
    }
}

// 打印空闲链表（用于调试）
void custom_allocator::printFreeList() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::cout << "Free List:\n";
    for (const auto &block : m_freeList) {
        std::cout << "  Address: " << block.first
            << ", Size: " << block.second << " bytes\n";
    }
}
