/* caches are stored in thread local memory */

#ifndef THREAD_CACHE_H_
#define THREAD_CACHE_H_

#include <stddef.h>
#include <stdbool.h>

/* maximum number of allocations that can be kept in a cache.
 * this is done to promote efficiency: if the cache is significantly
 * allowed to become significantly larger than the rest of the free lists, 
 * performance will suffer.
 */
#define CACHE_MAX_ENTRIES 8

/* maximum amount of memory that can be kept in a single cache entry.
 * at the moment, we are limiting total cache size to 1 MB.
 */
#define CACHE_MAX_SIZE (1 << 20)


/* if the cache is full, we evict from it with
 * some probability. note that it's not always
 * optimal to evict in 100% of the cases, since
 * blocks currently in the cache might still be
 * useful for future allocations.
 */
#define CACHE_EVICT_PROBABILITY (0.1f)

typedef struct block block_t;

typedef struct cache { 
    block_t *elems[CACHE_MAX_ENTRIES];

    /* number of entries in the cache. */
    size_t num_entries;

    /* total size of the cache. */
    size_t total_size;

    /* front-most entry. */
    int front;
} cache_t;

bool cache_full(cache_t *c);
bool cache_add(cache_t *c, block_t *block);
block_t *cache_evict(cache_t *c);
void cache_init(cache_t *c);

block_t *cache_query(cache_t *c, size_t size);

#endif