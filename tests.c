#include "malloc.h"
#include <pthread.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

#if defined (TEST_ARENA_ONLY)
    #define test_malloc arena_malloc
    #define test_free arena_free
    #define test_init arena_malloc_init

#elif defined (TEST_ARENA_CACHE)
    #define test_malloc arena_cached_malloc
    #define test_free arena_cached_free
    #define test_init arena_cached_malloc_init

#elif defined (TEST_NAIVE)
    #define test_malloc naive_malloc
    #define test_free naive_free
    #define test_init naive_malloc_init
#else
    #define test_malloc malloc
    #define test_free free
    #define test_init() (true)
#endif


#define MAX_MALLOC_LG  12

struct args_for_thread {
    int size;
    void *malloc_addr;
};

typedef struct args_for_thread a4t;

//stress tests for malloc

//generic thread
void *malloc_test_thread(void *arg) {  

#ifdef TEST_ARENA_CACHE
    /* initialize the thread cache, which is stored in
     * thread-local storage.
     */
    init_tcache();
#endif

    const double free_probability = (double) 0.1;

    size_t num_mallocs = (size_t) arg;
    void **pointas = test_malloc(sizeof(void *) * num_mallocs);
    assert(pointas);
    int top = 0;

    for (int i = 0; i < num_mallocs; i++) {
        void *ptr = test_malloc(1 << (rand() % MAX_MALLOC_LG));
        if (ptr)
            pointas[top++] = ptr;
        if (top > 0 && (double) rand() / RAND_MAX < free_probability) {
            test_free(pointas[--top]);
        }
    }

    while (top > 0)
        test_free(pointas[--top]);

    test_free(pointas);
    return NULL;
}


void *malloc_simple(void *arg) {
#ifdef TEST_ARENA_CACHE
    /* initialize the thread cache, which is stored in
     * thread-local storage.
     */
    init_tcache();
#endif
    size_t num_mallocs = (size_t) arg;
   for (int i = 0; i < num_mallocs; i++) {
       void *ptr = test_malloc(1 << (rand() % MAX_MALLOC_LG));
       if (ptr) free(ptr);
   }
   return NULL;
}

#if 0 

void *malloc_only_thread(a4t *args) {
    int size = args->size;
    args->malloc_addr = arena_malloc(size);
    if (!addr) error("malloc failed"); 
}

void free_only_thread(addr) {
    arena_free(addr);
}
#endif

void many_mallocs(size_t num_mallocs) {
    pthread_t thread_ids[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&(thread_ids[i]), NULL, &malloc_test_thread, (void *)num_mallocs);
    }

    for (int j = 0; j < NUM_THREADS; ++j) {
        pthread_join(thread_ids[j], NULL);
    }   
}

#if 0

//freeing on a different CPU
void malloc_and_free(int num_mallocs, int max_size) {
    pthread_t thread_ids[num_mallocs];
    void * malloc_addrs[num_mallocs];

    //allocate
    int malloc_size;
    a4t *args;
    for (int i = 0; i < num_mallocs; ++i) {
        malloc_size = rand();
        malloc_size %= max_size;
        malloc_size++;
        args.size = malloc_size;
        args.malloc_addr = &malloc_addrs[i];
        if (pthread_create(&(thread_ids[i]), NULL, &malloc_only_thread, args)) error("lol");
    }

    //join the allocating threads
    void *ret;
    for (int j = 0; j < num_mallocs; ++j) {
        if (pthread_join(&thread_ids[j], &ret)) error("reee");
    }

    //free
    int index;
    for (int k = 0; k < num_mallocs; ++k) {
        index = num_mallocs - 1 - k;
        void* ret;
        if (pthread_create(&thread_ids[k]), NULL, &free_only_thread, malloc_addrs(k)) error("err msg here");
        if (pthread_join(&thread_ids[k]), &ret) error("failed when joining");
    }
}
#endif

int main(int argc, const char* argv[]) {
    test_init();
    srand(time(0));

    clock_t start = clock();

    many_mallocs(100000);
    printf("Time taken for malloc test: %.7f\n", (double) (clock() - start) / CLOCKS_PER_SEC);
    return 0;
}