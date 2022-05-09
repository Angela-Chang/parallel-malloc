#ifndef MALLOC_H_
#define MALLOC_H_

#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

typedef uint64_t word_t;

enum {
    alloc_mask = 0x1,
    prev_alloc_mask = 0x2
};

#define CHUNK_SIZE  (1 << 12)

/* Maximum arena size will be kept at 128 MB.
 * Allocations bigger than that will just go
 * directly through mmap().
 */
#define ARENA_MAX_SIZE  (CHUNK_SIZE << 15)

/* each arena will reserve 16 KB at the time of its creation.
 * an arena may need to be resized later on, though.
 * Edit: currently, this is hard, so I am mmap()'ing
 * ARENA_MAX_SIZE when the arena is actually created,
 * since extending them with mremap() would require mmap()
 * putting the initial arena allocation at a "nice" address,
 * which I haven't been able to properly facilitate.
 */
#define ARENA_RESERVE   (CHUNK_SIZE << 3)

/* max number of lists per arena. */
#define MAXLISTS  15

/* pthread key associated with thread-local cache. */


typedef struct block block_t;

typedef struct arena {
    /* base of mmap()'ed region. not necessarily usable heap. */
    void *low;
    /* size of the entire mmap()'ed region. */
    size_t size;
    /* start of the usable heap. */
    void *heap_start;

    /* end of the usable heap. this is extended by a call
     * to extend_arena(). This does not correspond to the
     * end of the mmap()'ed region, which often has excess
     * memory in reserve (to avoid repeated mremap() system calls).
     */
    void *heap_end; 

    /* lists for heap lookup within this arena. */
    block_t *seglists[MAXLISTS];

    /* lock on arena usage. any allocations or frees taking place
     * on this arena must acquire this lock before proceeding.
     */
    pthread_mutex_t lock;
} arena_t;

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {
    /** @brief Header contains size + allocation flag. */
    word_t header;

    union {
        char payload[0];
        struct {
            struct block *prevBlockInList;
            struct block *nextBlockInList;
        };
    };
} block_t;

/* initializes the thread-local cache. the thread-local
 * storage location is provided by the gcc __thread keyword.
 */
void init_tcache(void);

size_t get_size(block_t *block);
size_t extract_size(word_t word);

word_t pack(size_t size, bool alloc, bool prev_alloc);

block_t *extend_arena_heap(arena_t *arena, size_t size, bool prev_alloc);

// possibly could just allow for malloc() to call this.
// in a multithreaded environment, it's simpler to just
// make one thread be responsible for calling this separately,
// before all invocations of arena_malloc(). we don't necessarily
// need to perfectly adhere to the original malloc() spec for the
// purposes of this project, so this should be fine.
bool arena_malloc_init(void);
bool arena_cached_malloc_init(void);
bool naive_malloc_init(void);

// returns an available arena (one not currently used)
// by any other processors.
arena_t *get_arena(void);
arena_t *find_arena(void *address);

void *arena_high(arena_t *arena);
void release_arena(arena_t *arena);
void *extend_arena(arena_t *arena, size_t length);
void arenas_init(int max_arenas);

void *naive_malloc(size_t);
void *arena_malloc(size_t);
void *arena_cached_malloc(size_t);

void naive_free(void *mem);
void arena_free(void *mem);
void arena_cached_free(void *mem);

#endif
