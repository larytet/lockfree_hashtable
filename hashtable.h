/**
 *   Lockfree is a set of lockfree containers for Linux and Linux kernel
 *   Copyright (C) <2017>  Arkady Miasnikov
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/**
 * Implementation of lockfree linear probing hashtable
 * The hashtable targets SystemTap environment where a typical key is thread ID
 * The number of probes is limited by a constant. The index is not wrapping around,
 * but instead the hashtable allocates enough memory to handle linear probing in the end
 * of the table
 *
 * Limitation: a specific entry (a specific key) can be inserted and deleted by one thread.
 *
 * Performance: a core can make above 13M add&remove operations per second, cost of a
 * single operation is under 20nano which is an equivalent of 50-100 opcodes.
 */

#pragma once


#ifdef __KERNEL__
#   include "linux/vmalloc.h"
#   include "linux/printk.h"
#   define DEV_NAME "lockless"
#   define PRINTF(s, ...) printk(KERN_ALERT DEV_NAME ": %s: " s "\n", __func__, __VA_ARGS__)
#   define PRIu64 "llu"
#else
#   include <stdlib.h>
#   include <stdio.h>
#   include <inttypes.h>
#   define PRINTF(s, ...) printf("%s: " s "\n", __func__, __VA_ARGS__)
#   define likely(x)      __builtin_expect(!!(x), 1)   // !!(x) will return 1 for any x != 0
#   define unlikely(x)    __builtin_expect(!!(x), 0)
#   define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define __sync_access(x) (*(volatile __typeof__(*x) *) (x))

/**
 * Based on https://gist.github.com/badboy/6267743
 *   http://burtleburtle.net/bob/hash/integer.html
 * I replace the 'long key' by 'unsigned key' and unsigned Java right shifts
 * by regular C/C++ right shifts
 * I adopt the hash function for 22 (PID_MAX_LIMIT) bits integers
 */
static uint32_t hash32shift(uint32_t key)
{

    key = ~key + (key << 10); // key = (key << 15) - key - 1;
    key = key ^ (key >> 7);
    key = key + (key << 1);
    key = key ^ (key >> 2);
    key = key * 187;        // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 11);
    return key;
}

static uint32_t hash_none(uint32_t key)
{
    return key;
}

typedef struct
{
    uint64_t insert;
    uint64_t remove;
    uint64_t search;
    uint64_t collision;
    uint64_t overwritten;
    uint64_t insert_err;
    uint64_t remove_err;
    uint64_t search_ok;
    uint64_t search_err;
} hashtable_stat_t;

static const char *hashtable_stat_names[] = {
									    "Insert",
									    "Remove",
									    "Search",
									    "Collision",
									    "Overwritten",
									    "Insert_err",
									    "Remove_err",
									    "Search_ok",
									    "Search_err",
};

typedef struct
{
    const char *name;
    size_t bits;

    uint32_t (*hashfunction)(uint32_t);

    size_t __size;
    size_t __memory_size;
    hashtable_stat_t __stat;
    void *__table;
} hashtable_t;

static hashtable_t *hashtable_registry[64];

static int hashtable_show(char *buf, size_t len)
{
    size_t i;
    int rc;
    size_t chars = 0;
    const char** stat_name = &hashtable_stat_names[0];
    size_t fieds_in_stat = sizeof(hashtable_stat_t)/sizeof(uint64_t);
    rc = snprintf(buf+chars, len-chars, "\n%-25s %12s %12s %12s",
            "Name", "Size", "Memory", "Ops");
    chars += rc;

    while (fieds_in_stat--)
    {
        rc = snprintf(buf+chars, len-chars, " %12s", *stat_name);
        stat_name++;
        chars += rc;
    }
    rc = snprintf(buf+chars, len-chars, "\n");

    chars += rc;
    for (i = 0;i < ARRAY_SIZE(hashtable_registry);i++)
    {
        hashtable_t *hashtable = hashtable_registry[i];
        size_t fieds_in_stat = sizeof(hashtable_stat_t)/sizeof(uint64_t);
        uint64_t *stat;
        if (!hashtable)
            continue;

        rc = snprintf(buf+chars, len-chars, "%-25s %12zu %12zu %12" PRIu64,
        		hashtable->name, hashtable->__size, hashtable->__memory_size,
				hashtable->__stat.insert+hashtable->__stat.remove+hashtable->__stat.search);
        chars += rc;
        stat = (uint64_t *)&hashtable->__stat;
        while (fieds_in_stat--)
        {
            rc = snprintf(buf+chars, len-chars, " %12" PRIu64, *stat);
            stat++;
            chars += rc;
        }
        rc = snprintf(buf+chars, len-chars, "\n");
        chars += rc;
    }
    return chars;
}

static void hashtable_registry_add(hashtable_t *table)
{
    size_t i;
    for (i = 0;i < ARRAY_SIZE(hashtable_registry);i++)
    {
        hashtable_t *registry = hashtable_registry[i];
        if (registry == table)
        {
            PRINTF("Hashtable %s already registered", table->name);
            break;
        }
        if (!registry)
        {
            PRINTF("Register hashtable %s", table->name);
            hashtable_registry[i] = table;
            break;
        }
    }
}

static void hashtable_registry_remove(hashtable_t *table)
{
    size_t i;
    for (i = 0;i < ARRAY_SIZE(hashtable_registry);i++)
    {
        hashtable_t *registry = hashtable_registry[i];
        if (registry == table)
        {
            PRINTF("Remove hashtable %s from the registry", table->name);
            hashtable_registry[i] = NULL;
        }
    }
}

static inline size_t hashtable_get_index(const hashtable_t *hashtable, const uint32_t hash)                                   \
{                                                                                                                             \
    return hash & (hashtable->__size - 1);                                                                                    \
}                                                                                                                             \

static void *hashtable_alloc(size_t size)
{
#ifdef __KERNEL__
    void *p;
    size = PAGE_ALIGN(size);
    p = vmalloc(size);
    if (p)
    {
        unsigned long long adr = (unsigned long long) p;
        while (size > 0)
        {
            SetPageReserved(vmalloc_to_page((void *)adr));
            adr += PAGE_SIZE;
            size -= PAGE_SIZE;
        }
    }
    return p;
#else
    return malloc(size);
#endif
}

static void hashtable_free(void *p, size_t size)
{
#ifdef __KERNEL__
    if (p)
    {
        unsigned long long adr = (unsigned long long) p;
        while ((long) size > 0) {
            ClearPageReserved(vmalloc_to_page((void *)adr));
            adr += PAGE_SIZE;
            size -= PAGE_SIZE;
        }
        vfree(p);
    }
#else
    free(p);
#endif
}

static void hashtable_close(hashtable_t *hashtable)
{
    if (hashtable->__table)
    {
        hashtable_free(hashtable->__table, hashtable->__memory_size);
    }
    else
    {
        PRINTF("Failed to free null pointer for the hashtable %s", hashtable->name);
    }
    hashtable_registry_remove(hashtable);
}



#ifdef __KERNEL__
#    define HASHTABLE_CMPXCHG(key, val, new_val) cmpxchg(key, val, new_val)
#else
#    define HASHTABLE_CMPXCHG(key, val, new_val) __sync_val_compare_and_swap(key, val, new_val)
#endif

#if 1
#ifdef __KERNEL__
#   define HASHTABLE_BARRIER() barrier()
#else
#   define HASHTABLE_BARRIER() __sync_synchronize()
#endif
#else
#   define HASHTABLE_BARRIER()
#endif

#define HASHTABLE_SLOT_ADDR(hashtable, tokn, index) &(((hashtable_## tokn ##_slot_t*)hashtable->__table)[index])

/**
 * Illegal TID can be (PID_MAX_LIMIT+1)
 * Illegal data is 0 for TID, -1 for FD, etc (this is optional)
 */
#define DECLARE_HASHTABLE(tokn, data_type, max_tries, illegal_key, illegal_data)                                                  \
                                                                                                                                  \
    typedef struct                                                                                                                \
    {                                                                                                                             \
        volatile uint32_t key;                                                                                                    \
        data_type data;                                                                                                           \
    } hashtable_## tokn ## _slot_t;                                                                                               \
                                                                                                                                  \
    static void hashtable_## tokn ## _init_slot(hashtable_## tokn ## _slot_t *slot)                                               \
    {                                                                                                                             \
        slot->key = illegal_key;                                                                                                  \
        slot->data = illegal_data;                                                                                                \
    }                                                                                                                             \
                                                                                                                                  \
    /**                                                                                                                           \
     * Calculate number of slots in the hash table                                                                                \
     * I add max_tries on top to ensure that there are max_tries slots after the                                                  \
     * end of the table                                                                                                           \
     */                                                                                                                           \
    static size_t hashtable_## tokn ##_memory_size(const int bits)                                                                \
    {                                                                                                                             \
        size_t slots = (1 << bits) + max_tries;                                                                                   \
        return (sizeof(hashtable_## tokn ## _slot_t) * slots);                                                                    \
    }                                                                                                                             \
                                                                                                                                  \
    static int hashtable_## tokn ##_init(hashtable_t *hashtable)                                                                  \
    {                                                                                                                             \
        size_t memory_size = hashtable_## tokn ## _memory_size(hashtable->bits);                                                  \
        void *p = hashtable_alloc(memory_size);                                                                                   \
        size_t i;                                                                                                                 \
        if (p)                                                                                                                    \
        {                                                                                                                         \
            if (hashtable->hashfunction == NULL)                                                                                  \
            {                                                                                                                     \
                hashtable->hashfunction = hash32shift;                                                                            \
            }                                                                                                                     \
            hashtable->__size = (1 << hashtable->bits);                                                                           \
            hashtable->__memory_size = memory_size;                                                                               \
            hashtable->__table = p;                                                                                               \
            for (i = 0;i < (hashtable->__size+max_tries);i++)                                                                     \
            {                                                                                                                     \
                hashtable_## tokn ## _init_slot(HASHTABLE_SLOT_ADDR(hashtable, tokn, i));                                         \
            }                                                                                                                     \
			hashtable_registry_add(hashtable);                                                                                    \
            return 1;                                                                                                             \
        }                                                                                                                         \
        PRINTF("Failed to allocate %zu for the hashtable %s", memory_size, hashtable->name);                                      \
        return 0;                                                                                                                 \
    }                                                                                                                             \
                                                                                                                                  \
    /**                                                                                                                           \
     * Hash the key, get an index in the hashtable, try compare-and-set.                                                          \
     * If fails (not likely) try again with the next slot (linear probing)                                                        \
     * continue until success or max_tries is hit                                                                                 \
     */                                                                                                                           \
    static int hashtable_## tokn ##_insert(hashtable_t *hashtable, const uint32_t key, const data_type data)                      \
    {                                                                                                                             \
        const uint32_t hash = hashtable->hashfunction(key);                                                                       \
        const uint32_t index = hashtable_get_index(hashtable, hash);                                                              \
        /* I can do this for the last slot too - I allocated max_tries more slots */                                              \
        const uint32_t index_max = index+max_tries;                                                                               \
        hashtable_## tokn ##_slot_t *slot = HASHTABLE_SLOT_ADDR(hashtable, tokn, index);                                          \
        const hashtable_## tokn ##_slot_t *slot_max = HASHTABLE_SLOT_ADDR(hashtable, tokn, index_max);                            \
        hashtable->__stat.insert++;                                                                                               \
        for (;slot < slot_max;slot++)                                                                                             \
        {                                                                                                                         \
            uint32_t old_key = HASHTABLE_CMPXCHG(&slot->key, illegal_key, key);                                                   \
            if (likely(old_key == illegal_key)) /* Success */                                                                     \
            {                                                                                                                     \
                slot->data = data;                                                                                                \
                return 1;                                                                                                         \
            }                                                                                                                     \
            else if (old_key == key)                                                                                              \
			{                                                                                                                     \
                slot->data = data;                                                                                                \
                hashtable->__stat.overwritten++;                                                                                  \
                return 1;                                                                                                         \
			}                                                                                                                     \
            else                                                                                                                  \
            {                                                                                                                     \
                hashtable->__stat.collision++;                                                                                    \
            }                                                                                                                     \
        }                                                                                                                         \
                                                                                                                                  \
        hashtable->__stat.insert_err++;                                                                                           \
        return 0;                                                                                                                 \
    }                                                                                                                             \
                                                                                                                                  \
    /**                                                                                                                           \
     * Hash the key, get an index in the hashtable, find the relevant entry,                                                      \
     * read the pointer, remove using atomic operation                                                                            \
     * Only one context is allowed to remove a specific entry                                                                     \
     */                                                                                                                           \
    static int hashtable_## tokn ##_remove(hashtable_t *hashtable, const uint32_t key, data_type *data)                           \
    {                                                                                                                             \
        const uint32_t hash = hashtable->hashfunction(key);                                                                       \
        const uint32_t index = hashtable_get_index(hashtable, hash);                                                              \
        /* I can do this for the last slot too - I allocated max_tries more slots */                                              \
        const uint32_t index_max = index+max_tries;                                                                               \
        hashtable_## tokn ##_slot_t *slot = HASHTABLE_SLOT_ADDR(hashtable, tokn, index);                                          \
        const hashtable_## tokn ##_slot_t *slot_max = HASHTABLE_SLOT_ADDR(hashtable, tokn, index_max);                            \
        hashtable->__stat.remove++;                                                                                               \
        for (;slot < slot_max;slot++)                                                                                             \
        {                                                                                                                         \
            uint32_t old_key = slot->key;                                                                                         \
            if (likely(old_key == key))                                                                                           \
            {                                                                                                                     \
                if (data)                                                                                                         \
                {                                                                                                                 \
                    *data = slot->data;                                                                                           \
                }                                                                                                                 \
                __sync_access(&slot->data) = illegal_data;                                                                        \
                HASHTABLE_BARRIER();                                                                                              \
                __sync_access(&slot->key) = illegal_key;                                                                          \
                return 1;                                                                                                         \
            }                                                                                                                     \
        }                                                                                                                         \
                                                                                                                                  \
        hashtable->__stat.remove_err++;                                                                                           \
        return 0;                                                                                                                 \
    }                                                                                                                             \
                                                                                                                                  \
    /**                                                                                                                           \
     * Hash the key, get an index in the hashtable, find the relevant entry,                                                      \
     * read the pointer                                                                                                           \
     */                                                                                                                           \
    static int hashtable_## tokn ##_find(hashtable_t *hashtable, const uint32_t key, data_type *data)                             \
    {                                                                                                                             \
        const uint32_t hash = hashtable->hashfunction(key);                                                                       \
        const uint32_t index = hashtable_get_index(hashtable, hash);                                                              \
        /* I can do this for the last slot too - I allocated max_tries more slots */                                              \
        const uint32_t index_max = index+max_tries;                                                                               \
        hashtable_## tokn ##_slot_t *slot = HASHTABLE_SLOT_ADDR(hashtable, tokn, index);                                          \
        const hashtable_## tokn ##_slot_t *slot_max = HASHTABLE_SLOT_ADDR(hashtable, tokn, index_max);                            \
        hashtable->__stat.search++;                                                                                               \
        for (;slot < slot_max;slot++)                                                                                             \
        {                                                                                                                         \
            uint32_t old_key = slot->key;                                                                                         \
            if (old_key == key)                                                                                                   \
            {                                                                                                                     \
                *data = slot->data;                                                                                               \
                hashtable->__stat.search_ok++;                                                                                    \
                return 1;                                                                                                         \
            }                                                                                                                     \
        }                                                                                                                         \
        hashtable->__stat.search_err++;                                                                                           \
                                                                                                                                  \
        return 0;                                                                                                                 \
    }                                                                                                                             \

