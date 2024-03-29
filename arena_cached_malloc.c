/**
 * A 64-bit struct-based implicit free list memory allocator
 *
 * This dynamic memory allocator uses multiple arenas, along with
 * a per-thread cache.
 * 
 */

#include "malloc.h"
#include "thread_cache.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/mman.h>

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, no code gets generated for these */
/* The sizeof() hack is used to avoid "unused variable" warnings */
#define dbg_requires(expr) (sizeof(expr), 1)
#define dbg_assert(expr) (sizeof(expr), 1)
#define dbg_ensures(expr) (sizeof(expr), 1)
#define dbg_printheap(...) ((void)sizeof(__VA_ARGS__))

#endif

#define MAX_SEGLIST_SEARCH  15

static bool mm_checkheap(int x) { return true; }

/** Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);
// 8B

/** Double word size (bytes) */
static const size_t dsize = 2 * wsize;
// 16B

/** Minimum block size (bytes) */
static const size_t min_block_size = 2 * dsize;
// 32B

// 2048B, divisible by 16
// when heap is full, extend heap by this much

/* Global variables */

/* a thread-local cache for quick allocations */
static __thread cache_t local_cache;

/** Pointer to first block in the heap */

static size_t find_list_for_block(block_t *block);


/*
 * mem_heap_lo - return address of the first heap byte
 */
static void *mem_heap_lo(arena_t *arena) {
    return arena->heap_start;
}

/*
 * mem_heap_hi - return address of last heap byte
 */
static void *mem_heap_hi(arena_t *arena) {
    // CONFUSION OF THE HIGHEST ORDER
    return (void *)((char *)arena->heap_end - 1);
}

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details within your header comments for the functions above!     *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */
// void print_freelist();
/**
 *Returns the maximum of two integers.
 * input: x
 * input: y
 * return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

static size_t min(size_t x, size_t y) {
    return (x < y) ? x : y;
}

/**
 * Rounds `size` up to next multiple of n
 * input: size
 * input: n
 * return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    if (size <= n)
        return 2 * n;

    return n * ((size + (n - 1)) / n);
}


/**
 * Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * input: bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/**
 * Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * input: block
 * return A pointer to the block's payload
 */
static void *header_to_payload(block_t *block) {
    return (void *)(block->payload);
}

/**
 * Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * input: block
 * return A pointer to the block's footer
 */
static word_t *header_to_footer(block_t *block) {
    // printf("block %p\n", block);
    // printf("header %lx\n", block->header);
    // printf("footer %lx\n", *(word_t *)(block->payload + get_size(block) -
    // dsize));
    return (word_t *)(block->payload + get_size(block) - dsize);
}

/**
 * Given a block footer, returns a pointer to the corresponding
 *        header.
 * input: footer A pointer to the block's footer
 * return A pointer to the start of the block
 */

static bool get_alloc(block_t *block);

static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    dbg_ensures(!get_alloc((block_t *)((char *)footer + wsize - size)));
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * input: block
 * return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - wsize;
}

/**
 * Returns the allocation status of a given header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * input: word
 * return The allocation status correpsonding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * Returns the allocation status of a block, based on its header.
 * input: block
 * return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}
/**
 * Returns the previous block's allocation status of a given header
 * value.
 *
 * This is based on the second lowest bit of the header value.
 *
 * input: word
 * return The previous block's allocation status correpsonding to the word
 */
static bool extract_prev_alloc(word_t word) {
    return (bool)(word & prev_alloc_mask);
}
/**
 * Returns the allocation status of the previous block, based on its
 * header.
 * input: block
 * return The allocation status of the previous block
 */
static bool get_prev_alloc(block_t *block) {
    return extract_prev_alloc(block->header);
}
/**
 * Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * output: block The location to write the epilogue header
 */
static void write_epilogue(block_t *block, bool prev_alloc) {
    dbg_requires(block != NULL);
    block->header = pack(0, true, prev_alloc);
}

/* this returns negative error code if arena is already
 * at full capacity (so the resizing process fails)
 */


/**
 * Writes a block starting at the given address.
 *
 * This function writes both a header and footer, where the location of the
 * footer is computed in relation to the header.
 *
 *
 * output block The location to begin writing the block header
 * input: size The size of the new block
 * input: alloc The allocation status of the new block
 */
static void write_block(block_t *block, size_t size, bool alloc,
                        bool prev_alloc) {
    dbg_requires(block != NULL);
    dbg_requires(size >= 0);

    if (!alloc) { // free

        block->header = pack(size, alloc, prev_alloc);
        word_t *footerp = header_to_footer(block);

        *footerp = pack(size, alloc, prev_alloc);
        dbg_assert(block->header == *footerp);
    }

    else if (alloc) {
        block->header = pack(size, alloc, prev_alloc);
    }
}

/**
 * Finds the next consecutive block on the heap.
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * input: block A block in the heap
 * return The next consecutive block on the heap
 * requires that the block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0);
    return (block_t *)((char *)block + get_size(block));
}

/**
 * Finds the footer of the previous block on the heap.
 * input: block A block in the heap
 * return the location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}

/**
 * Finds the previous consecutive block on the heap.
 *
 * This is the previous block in the "implicit list" of the heap.
 *
 * The position of the previous block is found by reading the previous
 * block's footer to determine its size, then calculating the start of the
 * previous block based on its size.
 *
 * input: block A block in the heap
 * return The previous consecutive block in the heap
 * requires that the block is not the first block in the heap
 */
static block_t *find_prev(block_t *block) { // dont call on first
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0);
    dbg_requires(!get_prev_alloc(block));
    word_t *footerp = find_prev_footer(block);
    return footer_to_header(footerp);
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines ********/

/**
 * Adds a block to explicit seglist
 * input: block A block in the heap
 */
static void add_to_free_list(block_t *block, arena_t *arena) {
    block_t **seglists = arena->seglists;

    // IN AFRICA, THAT IS SOOO BAAADDD
    size_t list_ind = find_list_for_block(block);
    if (seglists[list_ind] == block)
        return;

    if (seglists[list_ind] == NULL) {

        seglists[list_ind] = block;
        seglists[list_ind]->prevBlockInList = NULL;
        seglists[list_ind]->nextBlockInList = NULL;

        dbg_assert(seglists[list_ind] != NULL);
        dbg_assert(seglists[list_ind]->prevBlockInList == NULL);
        dbg_assert(seglists[list_ind]->nextBlockInList == NULL);
        return;
    }

    block_t *oldStart = seglists[list_ind];
    block->nextBlockInList = seglists[list_ind];
    oldStart->prevBlockInList = block;
    block->prevBlockInList = NULL;
    seglists[list_ind] = block;

    dbg_assert(seglists[list_ind] != NULL);
    dbg_assert(seglists[list_ind]->prevBlockInList == NULL);
}

static void delete_from_free_list(block_t *block, arena_t *arena) {
    block_t **seglists = arena->seglists;

    size_t list_ind = find_list_for_block(block);

    if (seglists[list_ind] == NULL)
        return;

    block_t *next = block->nextBlockInList;
    block_t *prev = block->prevBlockInList;

    // found the block, reset ptrs
    if (prev && next) {
        prev->nextBlockInList = next;
        next->prevBlockInList = prev;
        block->prevBlockInList = NULL;
        block->nextBlockInList = NULL;

    } else if (prev && !next) { // last block
        prev->nextBlockInList = NULL;
    }

    else if (!prev && next) { // first block
        seglists[list_ind] = next;
        next->prevBlockInList = NULL;
    } else if (!prev && !next) { // only block
        seglists[list_ind] = NULL;
    }

    return;
}

/**
 *  *
 * <What does this function do?> join free blocks together
 * <What are the function's arguments?> ptr to block being freed
 * <What is the function's return value?> ptr to block
 * <Are there any preconditions or postconditions?> ptr not null, block is free
 *
 * input: block
 *
 */
static block_t *coalesce_block(block_t *block, arena_t *arena) {
    dbg_assert(block != NULL);
    dbg_assert(!get_alloc(block));

    block_t *next = find_next(block);
    bool prevAlloc = get_prev_alloc(block);
    bool nextAlloc = get_alloc(next);

    size_t currSize = get_size(block);
    size_t nextSize = get_size(next);

    if (prevAlloc && nextAlloc) {
        return block;
    }

    else if (prevAlloc && !nextAlloc) {
        delete_from_free_list(next, arena);
        write_block(block, currSize + nextSize, false, true);
    }

    else if (!prevAlloc && nextAlloc) {
        block_t *prev = find_prev(block);
        dbg_assert(find_next(prev) == block);
        assert(prev != block);

        size_t prevSize = get_size(prev);
        delete_from_free_list(prev, arena);
        write_block(prev, currSize + prevSize, false, get_prev_alloc(prev));
        block = prev;
    }

    else if (!prevAlloc && !nextAlloc) {
        block_t *prev = find_prev(block);
        dbg_assert(find_next(prev) == block);

        size_t prevSize = get_size(prev);
        assert(prev != block);

        delete_from_free_list(next, arena);
        delete_from_free_list(prev, arena);

        write_block(prev, currSize + prevSize + nextSize, false,
                    get_prev_alloc(prev));

        block = prev;
    }

    assert(block != NULL);
    next = find_next(block);
    write_block(next, get_size(next), get_alloc(next), false);
    return block;
}

/**
 *  *
 * <What does this function do?> Splits a block into 2 to malloc the first part
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * input: block
 * input: asize
 */
static void split_block(block_t *block, size_t asize, arena_t *arena) {
    dbg_requires(get_alloc(block));
    dbg_requires(asize > 0);

    size_t block_size = get_size(block);

    if ((block_size - asize) >= min_block_size) {
        block_t *block_next;
        write_block(block, asize, true, get_prev_alloc(block));
        block_next = find_next(block);
        write_block(block_next, block_size - asize, false, true);
        add_to_free_list(block_next, arena);
    }
    dbg_ensures(get_alloc(block));
}

/**
 *  *
 * <What does this function do?> Finds the list for this block's size
 * <What are the function's arguments?> ptr to block
 * <What is the function's return value?> index of the list it should go to
 * <Are there any preconditions or postconditions?> See contracts
 *
 * input: asize
 * return list_ind
 */
static size_t find_list_for_block(block_t *block) {
    dbg_requires(block);
    // shift asize right to find list
    // shift by 5 to min block size
    size_t size = get_size(block) >> 6;
    size_t list_ind = 0;
    while (size != 0 && list_ind < MAXLISTS - 1) {
        size >>= 1; // next power
        list_ind++;
    }
    dbg_ensures(0 <= list_ind && list_ind <= MAXLISTS - 1);
    return list_ind;
}

/**
 *  *
 * <What does this function do?> Looks for a fit in a seglist
 * <What are the function's arguments?> ptr to start of list, size needed
 * <What is the function's return value?> a block of decent fit
 * <Are there any preconditions or postconditions?> See contracts
 *
 * input: asize, list_start
 * return a block or NULL
 */
static block_t *search_list(block_t *list_start, size_t asize) {
    dbg_requires(asize > 0);
    if (!list_start) {
        return NULL;
    }
    size_t count = 0;
    block_t *best_fit = NULL;
    size_t fit_error = asize;
    size_t block_size;
    for (block_t *block = list_start; block != NULL;
         block = block->nextBlockInList) {
        count++;

        if (!block || count > MAX_SEGLIST_SEARCH)
            break;

        block_size = get_size(block);
        if (block_size >= asize) {

            if (!best_fit) {
                best_fit = block;
                fit_error = block_size - asize;

            } else if (block_size - asize < fit_error) {
                best_fit = block;
                fit_error = block_size - asize;
            }

            if (fit_error == 0)
                return best_fit;
        }
    }

    if (best_fit)
        return best_fit;

    return NULL; // no fit found
}

/**
 *  *
 * <What does this function do?> Finds a block given a size
 * <What are the function's arguments?> size required
 * <What is the function's return value?> pointer of good block
 * <Are there any preconditions or postconditions?> See contracts
 *
 * input: asize
 * return block or NULL
 */
static block_t *find_fit(size_t asize, arena_t *arena) {
    dbg_requires(asize > 0);
    // asize a multiple of 16, min 32
    // get the smallest list it can have a fit
    size_t size = asize >> 6; // 32 gives 0
    size_t min_list_ind = 0;
    while (size != 0) {
        size >>= 1; // next power
        min_list_ind++;
    }

    if (min_list_ind > MAXLISTS - 1) {
        min_list_ind = MAXLISTS - 1;
    }

    // search each possible list
    for (int list_ind = min_list_ind;
        list_ind < MAXLISTS &&
        list_ind <= min_list_ind + 1; list_ind++)
    {
        block_t *block = search_list(arena->seglists[list_ind], asize);
        if (block)
            return block;
        // found a fit
        // if not move to next list
    }
    // all lists looked at, no fit.
    return NULL;
}

void init_tcache(void) {
    cache_init(&local_cache);
}

bool arena_cached_malloc_init(void) {
    arenas_init(10); /* replace with 2 * number of CPUs or something */
    return true;
}

/**
 *  *
 * <What does this function do?> Allocates a block of correct size
 * <What are the function's arguments?> size
 * <What is the function's return value?> ptr to right block
 * <Are there any preconditions or postconditions?> See contracts
 *
 * input: size
 * return bp or NULL
 */
static void *_malloc(size_t size, arena_t *arena) {
    dbg_requires(size > 0);

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;
    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment
    // requirements
    asize = round_up(size + wsize, dsize);

    // Search the free list for a fit
    block = find_fit(asize, arena);

    // If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        extendsize = max(asize, CHUNK_SIZE);

        block = extend_arena_heap(arena,
            extendsize,
            extract_prev_alloc(
                *(word_t *)(mem_heap_hi(arena) - 7)));

        if (block == NULL) {
            return bp;
        }
    }

    // The block should be marked as free
    dbg_assert(!get_alloc(block));

    // Mark block as allocated
    size_t block_size = get_size(block);
    write_block(block, block_size, true, get_prev_alloc(block));
    delete_from_free_list(block, arena);

    // Try to split the block if too large
    split_block(block, asize, arena);

    // Propoagate status to the next block
    block_t *next = find_next(block);
    write_block(next, get_size(next), get_alloc(next), true);
    bp = header_to_payload(block);
    return bp;
}

/**
 *  *
 * <What does this function do?> Frees a block
 * <What are the function's arguments?> ptr to block
 * <What is the function's return value?> void
 * <Are there any preconditions or postconditions?> See contracts
 *
 * input: bp
 */

/**
 *  *
 * <What does this function do?> Malloc but all bits set to 0
 * <What are the function's arguments?> number of elements and size of an
 * element <What is the function's return value?> ptr to block <Are there any
 * preconditions or postconditions?> See contracts
 *
 * input: elements
 * input: size
 * return bp
 */
static void *_calloc(size_t elements, size_t size) {
    dbg_requires(elements > 0 && size > 0);
    void *bp;
    size_t asize = elements * size;

    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}


/* thread safe wrappers with global lock.
 * naive version.
 */
void *arena_cached_malloc(size_t size) {

    /* first, query the thread-local cache. */
    
    block_t *block = cache_query(&local_cache, size);    
    if (block) {        
        /* if that found something, we're done. */
        return header_to_payload(block);    
    }

    /* otherwise, find an arena to use, grab a lock
     * on it, and then proceed.
     */
    arena_t *arena = get_arena();

    if (!arena->heap_start) {
        assert(!arena->low);
        arena->low = mmap(NULL, ARENA_MAX_SIZE, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        arena->size = ARENA_MAX_SIZE;

        if (arena->low == MAP_FAILED) {
            arena->low = NULL;
            release_arena(arena);
            return NULL;
        }

        word_t *start = (word_t *)arena->low;
        start[0] = pack(0, true, true);
        start[1] = pack(0, true, true);

        // Heap starts with first "block header", currently the epilogue
        arena->heap_start = (block_t *)&(start[1]);
        arena->heap_end = (void *)((char *)start + 2 * wsize);

        if (extend_arena_heap(arena, CHUNK_SIZE, true) == NULL) {
            assert(false && "arena initialization failed");
        }
    }

    assert(arena && "there must always be a valid arena");
    void *output = _malloc(size, arena);

    /* relinquish our ownership of this arena, allowing another
     * thread to make use of it.
     */
    release_arena(arena);
    return output;
}

/* actually frees a block. this is different from
 * just freeing, which might end up just inserting
 * to the cache.
 */
void truly_free(block_t *block)
{
    arena_t *arena = find_arena((void *)block);
    size_t size = get_size(block);
    
    // Mark the block as free
    write_block(block, size, false, get_prev_alloc(block));
    block_t *next = find_next(block);
    write_block(next, get_size(next), get_alloc(next), false);

    // Try to coalesce the block with its neighbors
    block = coalesce_block(block, arena);
    add_to_free_list(block, arena);

    release_arena(arena);
}

/* we try to insert the
 * block to the cache for reuse and 
 *try to take advantage of possible locality
 */

void arena_cached_free(void *ptr) {
    block_t *block = payload_to_header(ptr);
    if (cache_add(&local_cache, block))
        return;
    
    /* if we failed to insert it into the cache,
     * evict from the cache with some probability,
     * which is currently fixed.
     */
    if ((double)(rand() / RAND_MAX) < CACHE_EVICT_PROBABILITY) {
        block_t *evict = cache_evict(&local_cache);   
        truly_free(evict);

        /* now try to put it into the cache one more time. */
        if (cache_add(&local_cache, block)) {
            return;
        }
    }
    
    /* if the cache refused our request, then we
     * will free the block back to the arena.
     */
    truly_free(block);
}
