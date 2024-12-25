#pragma once

#define PAGE_SHIFT 12

#define PAGE_SIZE (1UL << PAGE_SHIFT)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_ALIGN(x) ((static_cast<unsigned long>(x) + (PAGE_SIZE - 1)) & PAGE_MASK)

#define PAGE_ROUND_UP(x) ((static_cast<unsigned long>(x) + PAGE_SIZE - 1) & PAGE_MASK)

#define PAGE_ROUND_DOWN(x) (static_cast<unsigned long>(x) & PAGE_MASK)

#define PFN_2_PHYS(x) (static_cast<unsigned long>(x) << PAGE_SHIFT)

#define PHYS_2_PFN(x) (static_cast<unsigned long>(x) >> PAGE_SHIFT)

#define IS_PAGE_ALIGN(x) ((static_cast<unsigned long>(x) & (PAGE_SIZE - 1)) == 0)