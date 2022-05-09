#include "malloc.h"

/**
 * @brief Packs the `size` and `alloc` of a block into a word suitable for
 *        use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc True if the block is allocated
 * @return The packed value
 */
word_t pack(size_t size, bool alloc, bool prev_alloc) {
    word_t word = size;
    if (alloc) {
        word |= alloc_mask;
    }
    if (prev_alloc) {
        word |= prev_alloc_mask;
    }
    // printf("word: %lx\n", word);
    return word;
}


/* size is stored in the upper bits. */
static const word_t size_mask = ~(word_t)0xF;

size_t extract_size(word_t word) {
    return (word & size_mask);
}

size_t get_size(block_t *block) {
    return extract_size(block->header);
}