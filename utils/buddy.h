#pragma once
#include <sys/queue.h>
#include <stdint.h>
/**
 * @file buddy.h
 * @brief Buddy-system memory allocator
 */

typedef enum purpose { FREE, USED } purpose_t;

#define BUDDY_ALIGN_PREF (32 - 2 * sizeof(void*) - sizeof(uint32_t) - sizeof(purpose_t))

/**
 * Metadata about this particular block
 * (and stored at the beginning of this block).
 * One per allocated block of memory.
 * Should be 32 bytes to not screw up alignment.
 */
struct buddy_list {
    // Should be two pointers
    LIST_ENTRY(buddy_list) next_freelist;
    uint32_t size;
    purpose_t use;
    char padding[BUDDY_ALIGN_PREF];
};

typedef enum valid { VALID, INVALID } valid_t;

/**
 * Bucket of 2^order sized free memory blocks.
 */
struct buddy_list_bucket {
public:
    LIST_HEAD(buddy_list_head, buddy_list) ptr;
    unsigned int count;
    unsigned int order;
    valid_t is_valid;
    void *buddy_base_address;

    void *buddy_alloc(uint64_t size);
    void buddy_free(void *ptr);
    int dump_buddy_table();

    buddy_list_bucket() {};
    ~buddy_list_bucket();

    static buddy_list_bucket *create_buddy_table(size_t numa_node, unsigned int power_of_two);
private:
    int buddy_try_merge(buddy_list *blt);
    void buddy_split(buddy_list_bucket *bucket);
};
