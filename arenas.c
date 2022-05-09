#include "malloc.h"

#include <assert.h>
#include <unistd.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdlib.h>

static arena_t *arenas = NULL;
static int max_arenas = 0;

static pthread_mutex_t arena_lock;
static int last_used = 0;

void arenas_init(int num_arenas) {

    pthread_mutex_init(&arena_lock, NULL);

    assert(num_arenas > 0);
    max_arenas = num_arenas;

    size_t bytes = max_arenas * sizeof(arena_t);
    arenas = mmap(NULL, bytes, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    assert(arenas != MAP_FAILED && "failed to create arena buffer");

    for (int i = 0; i < max_arenas; i++) {
        pthread_mutex_init(&arenas[i].lock, NULL);
        arena_t *arena = &arenas[i];
        arena->low = mmap(NULL, ARENA_MAX_SIZE, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        arena->size = ARENA_MAX_SIZE;

        assert(arena->low != MAP_FAILED);

        word_t *start = (word_t *)arena->low;
        start[0] = pack(0, true, true);
        start[1] = pack(0, true, true);

        // Heap starts with first "block header", currently the epilogue
        arena->heap_start = (block_t *)&(start[1]);
        arena->heap_end = (void *)((char *)start + 2 * sizeof(uint64_t));

        if (extend_arena_heap(arena, CHUNK_SIZE, true) == NULL) {
            assert(false && "arena initialization failed");
        }
    }
}

// precondition: lock on arena is already held
// (presumably by a call to get_arena() or find_arena())
void *extend_arena(arena_t *arena, size_t length) {
    void *result = arena->heap_end;
    void *new_end = (void *)((char *)arena->heap_end + length);
    if ((char *)new_end > (char *)arena->low + arena->size) {
        // TODO: do something with mremap().
        return NULL;
    } else {
        arena->heap_end = new_end;
        return result;
    }
}

// fetches an available arena, or waits for one
// to open up.
arena_t *get_arena(void) {
    assert(max_arenas > 0);

    arena_t *chosen_arena = NULL;
    int index = __atomic_fetch_add(&last_used, 1, __ATOMIC_SEQ_CST) % max_arenas;
    pthread_mutex_lock(&arenas[index].lock);
    return &arenas[index];
}

// finds the arena associataed with the given
// address by checking the list.
arena_t *find_arena(void *address) {
    for (int i = 0; i < max_arenas; i++) {
        if (address >= arenas[i].heap_start && address <= arenas[i].heap_end) {
            pthread_mutex_lock(&arenas[i].lock);
            return &arenas[i];
        }
    }
    return NULL;
}

void release_arena(arena_t *arena) {
    pthread_mutex_unlock(&arena->lock);
}
