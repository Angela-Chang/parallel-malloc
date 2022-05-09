/**
 * @file thread_cache.c
 * @brief a per-thread cache for the parallel malloc implementation
 *
 * The per-thread cache is implemented as a ring buffer of
 * blocks. Blocks will be held in the thread cache, and will
 * be evicted back into their corresponding arenas when
 * the cache has reached capacity. A capacity limit
 * should be set so that no cache holds on to too much
 * memory (it would be inappropriate for one thread to steal
 * several megabytes from the rest of the system, for example).
 * 
 * @author Angela Chang <achang2@andrew.cmu.edu>
 */


#include "thread_cache.h"
#include "malloc.h"

#include <assert.h>
#include <string.h>

void cache_init(cache_t *c) {
    memset(c->elems, 0, sizeof(c->elems));

    c->num_entries = 0;
    c->total_size = 0;
    
    /* this is used to indicate that there is nothing
     * currently in the cache.
     */
    c->front = CACHE_MAX_ENTRIES;
}

bool cache_add(cache_t *c, block_t *block) {    
    if (c->num_entries == CACHE_MAX_ENTRIES)
        return false;
    
    size_t bsize = get_size(block);
    if (c->total_size + bsize > CACHE_MAX_SIZE)
        return false;
    
    /* max cache entries is really small (currently 8), 
     * so can afford to do this without impacting performance
     * at all.
     */
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if (!c->elems[i]) {
            c->num_entries++;
            c->total_size += bsize;
            c->elems[i] = block;
            if (i < c->front) {
                c->front = i;
            }
            break;
        }
    }

    return true;
}

block_t *cache_evict(cache_t *c) {   
    assert(c->num_entries > 0);
    c->total_size -= get_size(c->elems[c->front]);
    c->num_entries--;

    block_t *result = c->elems[c->front];
    c->elems[c->front] = NULL;
    while (c->front < CACHE_MAX_ENTRIES && !c->elems[c->front])
        c->front++;
    
    return result;
}

block_t *cache_query(cache_t *c, size_t size) {
    for (int i = c->front; i < CACHE_MAX_ENTRIES; i++) {
        if (!c->elems[i]) continue;

        block_t *b = c->elems[i];
        size_t bsize = get_size(b);
        if (bsize >= size) {            
            c->elems[i] = NULL;
            c->total_size -= bsize;
            c->num_entries--;
            if (i == c->front) {
                while (c->front < CACHE_MAX_ENTRIES && !c->elems[c->front]) {
                    c->front++;
                }
            }

            return b;
        }
    }

    return NULL;
}