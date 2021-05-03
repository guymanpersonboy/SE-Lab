/*
 * cache.c - A cache simulator that can replay traces from Valgrind
 *     and output statistics such as number of hits, misses, and
 *     evictions, both dirty and clean.  The replacement policy is LRU. 
 *     The cache is a writeback cache. 
 * 
 * Updated 2021: M. Hinton
 */
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include "cache.h"
static uword_t get_set_index(cache_t *cache, uword_t addr);
static uword_t get_block_offset(cache_t *cache, uword_t addr);

#define ADDRESS_LENGTH 64

/* Counters used to record cache statistics in printSummary().
   test-cache uses these numbers to verify correctness of the cache. */

//Increment when a miss occurs
int miss_count = 0;

//Increment when a hit occurs
int hit_count = 0;

//Increment when a dirty eviction occurs
int dirty_eviction_count = 0;

//Increment when a clean eviction occurs
int clean_eviction_count = 0;

// TODO: add more globals, structs, macros if necessary
// save correct way if a check hit for use in get_line()
static unsigned int way = 0;

/*
 * Initialize the cache according to specified arguments
 * Called by cache-runner so do not modify the function signature
 *
 * The code provided here shows you how to initialize a cache structure
 * defined above. It's not complete and feel free to modify/add code.
 */
cache_t *create_cache(int s_in, int b_in, int E_in, int d_in)
{
    /* see cache-runner for the meaning of each argument */
    cache_t *cache = malloc(sizeof(cache_t));
    cache->s = s_in;
    cache->b = b_in;
    cache->E = E_in;
    cache->d = d_in;
    unsigned int S = (unsigned int) pow(2, cache->s);
    unsigned int B = (unsigned int) pow(2, cache->b);

    cache->sets = (cache_set_t*) calloc(S, sizeof(cache_set_t));
    for (unsigned int i = 0; i < S; i++){
        cache->sets[i].lines = (cache_line_t*) calloc(cache->E, sizeof(cache_line_t));
        for (unsigned int j = 0; j < cache->E; j++){
            cache->sets[i].lines[j].valid = 0;
            cache->sets[i].lines[j].tag   = 0;
            cache->sets[i].lines[j].lru   = 0;
            cache->sets[i].lines[j].dirty = 0;
            cache->sets[i].lines[j].data  = calloc(B, sizeof(byte_t));
        }
    }

    // TODO: add more code for initialization

    return cache;
}

cache_t *create_checkpoint(cache_t *cache) {
    unsigned int S = (unsigned int) pow(2, cache->s);
    unsigned int B = (unsigned int) pow(2, cache->b);
    cache_t *copy_cache = malloc(sizeof(cache_t));
    memcpy(copy_cache, cache, sizeof(cache_t));
    copy_cache->sets = (cache_set_t*) calloc(S, sizeof(cache_set_t));
    for (unsigned int i = 0; i < S; i++) {
        copy_cache->sets[i].lines = (cache_line_t*) calloc(cache->E, sizeof(cache_line_t));
        for (unsigned int j = 0; j < cache->E; j++) {
            memcpy(&copy_cache->sets[i].lines[j], &cache->sets[i].lines[j], sizeof(cache_line_t));
            copy_cache->sets[i].lines[j].data = calloc(B, sizeof(byte_t));
            memcpy(copy_cache->sets[i].lines[j].data, cache->sets[i].lines[j].data, sizeof(byte_t));
        }
    }
    
    return copy_cache;
}

void display_set(cache_t *cache, unsigned int set_index) {
    unsigned int S = (unsigned int) pow(2, cache->s);
    if (set_index < S) {
        cache_set_t *set = &cache->sets[set_index];
        for (unsigned int i = 0; i < cache->E; i++) {
            printf ("Valid: %d Tag: %llx Lru: %lld Dirty: %d\n", set->lines[i].valid, 
                set->lines[i].tag, set->lines[i].lru, set->lines[i].dirty);
        }
    } else {
        printf ("Invalid Set %d. 0 <= Set < %d\n", set_index, S);
    }
}

/*
 * Free allocated memory. Feel free to modify it
 */
void free_cache(cache_t *cache)
{
    unsigned int S = (unsigned int) pow(2, cache->s);
    for (unsigned int i = 0; i < S; i++){
        for (unsigned int j = 0; j < cache->E; j++) {
            free(cache->sets[i].lines[j].data);
        }
        free(cache->sets[i].lines);
    }
    free(cache->sets);
    free(cache);
}

/* // TODO:
 * Get the line for address contained in the cache
 * On hit, return the cache line holding the address
 * On miss, returns NULL
 */
cache_line_t *get_line(cache_t *cache, uword_t addr)
{
    /* your implementation */
    uword_t set_index = get_set_index(cache, addr);

    if (check_hit(cache, addr, false)) {
        return &cache->sets[set_index].lines[way];
    }

    return NULL;
}

/* // TODO:
 * Select the line to fill with the new cache line
 * Return the cache line selected to filled in by addr
 */
cache_line_t *select_line(cache_t *cache, uword_t addr)
{
    /* your implementation */
    uword_t set_index = get_set_index(cache, addr);

    uword_t lru;
    unsigned int lru_way;
    unsigned int i;
    for (i = 0; i < cache->E; i++) {
        // keep track of lru in case we need it
        if (cache->sets[set_index].lines[i].lru < lru) {
            lru = cache->sets[set_index].lines[i].lru;
            lru_way = i;
        }
        
        // Case R2a: cache miss, no replacement
        if (cache->sets[set_index].lines[i].valid == false) {
            return &cache->sets[set_index].lines[i];
            break;
        }
    }
    // Case R2b: cache miss, replacement
    return &cache->sets[set_index].lines[lru_way];
}

/* // TODO:
 * Check if the address is hit in the cache, updating hit and miss data.
 * Return true if pos hits in the cache.
 */
bool check_hit(cache_t *cache, uword_t addr, operation_t operation)
{
    /* your implementation */
    uword_t set_index = get_set_index(cache, addr);
    // right shift out set index and block offset
    uword_t tag = addr >> (cache->s + cache->b);

    for (unsigned int i = 0; i < cache->E; i++) {
        if (cache->sets[set_index].lines[i].valid && cache->sets[set_index].lines[i].tag == tag) {
            hit_count++;
            way = i;
            // TODO update dirty bit?
            if (operation == WRITE) {
                cache->sets[set_index].lines[i].dirty = true;
                dirty_eviction_count++;
            }
            
            return true;
        }
    }
    // false valid bit or incorrect tag
    miss_count++;
    return false;
}

/* // TODO:
 * Handles Misses, evicting from the cache if necessary.
 * Fill out the evicted_line_t struct with info regarding the evicted line.
 */
evicted_line_t *handle_miss(cache_t *cache, uword_t addr, operation_t operation, byte_t *incoming_data)
{
    size_t B = (size_t)pow(2, cache->b);
    evicted_line_t *evicted_line = malloc(sizeof(evicted_line_t));
    evicted_line->data = (byte_t *) calloc(B, sizeof(byte_t));

    /* your implementation */
    uword_t set_index = get_set_index(cache, addr);
    uword_t block_offset = get_block_offset(cache, addr);
    cache_line_t *line = select_line(cache, addr);

    evicted_line->valid = line->valid;
    // TODO make address for evicted line??
    evicted_line->addr =
        ((line->tag << (cache->s + cache->b)) | (set_index << cache->b) | block_offset);
    evicted_line->data = line->data;

    if (line->dirty) {
        dirty_eviction_count++;
        evicted_line->dirty = true;
    } else {
        clean_eviction_count++;
        evicted_line->dirty = false;
    }

    // TODO operation, incoming data

    return evicted_line;
}

/* // TODO:
 * Get a byte from the cache and write it to dest.
 * Preconditon: pos is contained within the cache.
 */
void get_byte_cache(cache_t *cache, uword_t addr, byte_t *dest)
{
    /* your implementation */
    uword_t block_offset = get_block_offset(cache, addr);
    cache_line_t *line = get_line(cache, addr);
    // refelcts get_byte_val of isa.c
    *dest = line->data[block_offset];
}


/* // TODO:
 * Get 8 bytes from the cache and write it to dest.
 * Preconditon: pos is contained within the cache.
 */
void get_word_cache(cache_t *cache, uword_t addr, word_t *dest)
{
    /* your implementation */
    uword_t block_offset = get_block_offset(cache, addr);
    cache_line_t *line = get_line(cache, addr);
    // reflects get_word_val of isa.c
    word_t val = 0;
    for (int i = 0; i < 8; i++) {
        word_t b = line->data[i + block_offset] && 0xFF;
        val = val | (b << (8 * i));
    }
    *dest = val;
}


/* // TODO:
 * Set 1 byte in the cache to val at pos.
 * Preconditon: pos is contained within the cache.
 */
void set_byte_cache(cache_t *cache, uword_t addr, byte_t val)
{
    /* your implementation */
    uword_t block_offset = get_block_offset(cache, addr);
    cache_line_t *line = get_line(cache, addr);
    // reflects set_byte_val of isa.c
    line->data[block_offset] = val;
    // TODO dirty??
    line->dirty = true;
}


/* // TODO:
 * Set 8 bytes in the cache to val at pos.
 * Preconditon: pos is contained within the cache.
 */
void set_word_cache(cache_t *cache, uword_t addr, word_t val)
{
    /* your implementation */
    uword_t block_offset = get_block_offset(cache, addr);
    cache_line_t *line = get_line(cache, addr);
    // reflects set_word_val of isa.c
    for (int i = 0; i < 8; i++) {
        line->data[block_offset + i] = (byte_t) val & 0xFF;
        val >>= 8;
    }
    // TODO dirty??
    line->dirty = true;
}

/*
 * retrieve set_index from addr and return the value
 */
static uword_t get_set_index(cache_t *cache, uword_t addr) {
    // left shift out tag: t = m - s - b
    uword_t set_index = addr << (ADDRESS_LENGTH - cache->s - cache->b);
    // move to lower end: t + b = m - s
    set_index >>= (ADDRESS_LENGTH - cache->s);
    return set_index;
}

/*
 * retrieve block_offset from addr and returnt the value
 */
static uword_t get_block_offset(cache_t *cache, uword_t addr) {
    // left shift out tag and set index: t + s = m - b
    uword_t block_offset = addr << (ADDRESS_LENGTH - cache->b);
    // undo to move back to lower end
    block_offset >>= (ADDRESS_LENGTH - cache->b);
    return block_offset;
}

/*
 * Access data at memory address addr
 * If it is already in cache, increast hit_count
 * If it is not in cache, bring it in cache, increase miss count
 * Also increase eviction_count if a line is evicted
 *
 * Called by cache-runner; no need to modify it if you implement
 * check_hit() and handle_miss()
 */
void access_data(cache_t *cache, uword_t addr, operation_t operation)
{
    if(!check_hit(cache, addr, operation))
        free(handle_miss(cache, addr, operation, NULL));
}