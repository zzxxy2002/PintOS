#include "buffer_cache.h"
#include "inode.h"
#include "utils.h"

struct cached_sector {
  block_sector_t sector_idx; /* Sector number; -1 if not in use. */
  bool dirty; /* True if the cache has unflushed modifications. */
  int64_t last_accessed; /* Last time the cache is accessed. If cache is inactive, this value is INT64_MIN. synchronized using the global lock. */
  rwLock lock; /* Read-write lock for data accesses. */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Cached data. */
};


struct buffer_cache {
  struct lock lock; /* Global lock for the buffer cache, acquired when updating/searching for entries.*/
  struct block* block_device; /* Block device to cache. */
  int num_hit;
  int num_miss;
  struct cached_sector sectors[BUFFER_CACHE_SIZE]
};

/*Write back data from A_CACHE into its corresponding sector in disk, if the cache is dirty. .*/
static void cached_sector_flush(buffer_cache_t* a_cache, struct cached_sector* a_sector) {
  ASSERT(lock_held_by_current_thread(&a_cache->lock));
  if (a_sector->last_accessed == INT64_MIN) {
    return;
  }
  wLock_acquire(&a_sector->lock);
  if (a_sector->dirty) {
    // write back
    block_write(a_cache->block_device, a_sector->sector_idx, a_sector->data);
    // update dirty status
    a_sector->dirty = false;
  }
  wLock_release(&a_sector->lock);
}

/*Find the cached buffer from the cache, or cache the sector and then return the cached buffer, evicting any old buffer if necessary.
  A_LOAD_DATA is set to false ONLY when loading data isn't necessary for the cache to function i.e. exactly one block of data is being written to the cache.*/
static struct cached_sector* buffer_cache_fetch(buffer_cache_t* a_cache, block_sector_t a_sector, bool a_load_data) {
  struct cached_sector* lru_sector = NULL;
  struct cached_sector* ret = NULL;
  bool hit = false;
  int64_t lru_accessed = INT64_MAX;
  // Find
  lock_acquire(&a_cache->lock);

  for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
    struct cached_sector* one_sector = a_cache->sectors + i;
    if (one_sector->sector_idx == a_sector) {
      ret = one_sector;
      hit = true;
      break;
    }
    // Find the least recently used sector; note that last_accessed is initialized to INT64_MIN, so any uninitialized sector will be prioritized.
    if (one_sector->last_accessed < lru_accessed) {
      lru_accessed = one_sector->last_accessed;
      lru_sector = one_sector;
    }
  }

  // Insertion
  if (!hit) {
    a_cache->num_miss++;
    ASSERT(lru_sector != NULL); // sanity check
    // Write back (if)on eviction
    cached_sector_flush(a_cache, lru_sector);
    // Data fetch
    lru_sector->sector_idx = a_sector;
    if (a_load_data) {
      wLock_acquire(&lru_sector->lock);
      block_read(a_cache->block_device, a_sector, lru_sector->data);
      wLock_release(&lru_sector->lock);
    }
    ret = lru_sector;
  } else {
    a_cache->num_hit++;
  }

  ASSERT(ret != NULL);
  // Update for LRU
  ret->last_accessed = timer_ticks();

  lock_release(&a_cache->lock);

  return ret;
}

/* Initialize buffer cache; This function is called only once.*/
static void buffer_cache_init(buffer_cache_t* a_cache) {
  lock_init(&a_cache->lock);
  for (size_t i = 0; i < BUFFER_CACHE_SIZE; i++) {
    struct cached_sector* one_sector = a_cache->sectors + i;
    one_sector->sector_idx = -1;
    one_sector->dirty = false;
    one_sector->last_accessed = INT64_MIN; // INT64_MIN means the sector is not in use.
    a_cache->num_hit = 0;
    a_cache->num_miss = 0;
    rwLock_init(&one_sector->lock);
  }
}

/* Write A_SIZE bytes from A_SRC to the block A_DEST, starting at A_DEST, through buffer cache.*/
void buffer_cache_write(buffer_cache_t* a_cache, block_sector_t a_dest, void* a_src, int a_offset, int a_size) {
  ASSERT(a_dest != -1);
  bool load_data = a_offset != 0 || a_size != BLOCK_SECTOR_SIZE; // avoid unnecessary disk access for write
  struct cached_sector* sector = buffer_cache_fetch(a_cache, a_dest, load_data);
  wLock_acquire(&sector->lock);
  memcpy(sector->data + a_offset, a_src, a_size);
  sector->dirty = true;
  wLock_release(&sector->lock);
}

/* Read A_SIZE bytes from the block A_SRC, starting at A_OFFSET, into A_DEST, through buffer cache.*/
void buffer_cache_read(buffer_cache_t* a_cache, block_sector_t a_src, void* a_dest, int a_offset, int a_size) {
  ASSERT(a_src != -1);
  struct cached_sector* sector = buffer_cache_fetch(a_cache, a_src, true);
  rLock_acquire(&sector->lock);
  memcpy(a_dest, sector->data + a_offset, a_size);
  rLock_release(&sector->lock);
}

/*Flush all dirty blocks in the buffer cache.*/
void buffer_cache_flush(buffer_cache_t* a_cache) {
  lock_acquire(&a_cache->lock);
  // no lock is needed for the list since all elements have their own locks.
  for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
    struct cached_sector* one_sector = a_cache->sectors + i;
    cached_sector_flush(a_cache, one_sector);
  }
  lock_release(&a_cache->lock);
}

/*Reset all cache as cold and flush unflushed writes.*/
void buffer_cache_reset(buffer_cache_t* a_cache) {
  lock_acquire(&a_cache->lock);
  for (int i = 0; i < BUFFER_CACHE_SIZE; i++) {
    struct cached_sector* one_sector = a_cache->sectors + i;
    cached_sector_flush(a_cache, one_sector);
    one_sector->sector_idx = -1;
    one_sector->dirty = false;
    one_sector->last_accessed = INT64_MIN;
  }
  a_cache->num_hit = 0;
  a_cache->num_miss = 0;
  lock_release(&a_cache->lock);
}

int buffer_cache_get_hit_time(buffer_cache_t* a_cache) {
  lock_acquire(&a_cache->lock);
  int ret = a_cache->num_hit;
  lock_release(&a_cache->lock);
  return ret;
}
int buffer_cache_get_miss_time(buffer_cache_t* a_cache) {
  lock_acquire(&a_cache->lock);
  int ret = a_cache->num_miss;
  lock_release(&a_cache->lock);
  return ret;
}

/* Dynamically allocate space for a new buffer cache designated for A_BLOCK_DEVICE.*/
buffer_cache_t* buffer_cache_create(struct block* a_block_device) {
  buffer_cache_t* ret = malloc(sizeof(buffer_cache_t));
  if (!ret) {
    return NULL;
  }
  ret->block_device = a_block_device;
  buffer_cache_init(ret);
  return ret;
}

