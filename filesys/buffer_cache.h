#include "devices/block.h"

#define ENABLE_BUFFER_CACHE 1


#define BUFFER_CACHE_SIZE 64 /*Maximum number of sectors to be cached. */

struct buffer_cache;

typedef struct buffer_cache buffer_cache_t;

void buffer_cache_flush(buffer_cache_t* a_cache);
void buffer_cache_reset(buffer_cache_t* a_cache);
int buffer_cache_get_hit_time(buffer_cache_t* a_cache);
int buffer_cache_get_miss_time(buffer_cache_t* a_cache);

void buffer_cache_read(buffer_cache_t* a_cache, block_sector_t a_src, void* a_dest, int a_offset, int a_size);
void buffer_cache_write(buffer_cache_t* a_cache, block_sector_t a_dest, void* a_src, int a_offset, int a_size);

buffer_cache_t* buffer_cache_create(struct block* a_block_device);