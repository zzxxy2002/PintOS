#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/utils.h"
#include "buffer_cache.h"

/*Read a whole block of data using either buffer cache or direcly from block, depending on whether buffer cache is active.*/
#define FS_READ_BLOCK(src, dst) \
   if(ENABLE_BUFFER_CACHE) {buffer_cache_read(fs_buffer_cache, src, dst, 0, BLOCK_SECTOR_SIZE);} \
   else {block_read(fs_device, src, dst);}

#define FS_WRITE_BLOCK(src, dst) \
    if(ENABLE_BUFFER_CACHE) {buffer_cache_write(fs_buffer_cache, dst, src, 0, BLOCK_SECTOR_SIZE);} \
    else {block_write(fs_device, dst, src);}
    
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/*A direct inode data block, storing BLOCK_SECTOR_SIZE(512) bytes*/
typedef struct {
  char data[BLOCK_SECTOR_SIZE];
} inode_data_block_t;

#define INDIRECT_BLOCK_NUM_ENTRIES (BLOCK_SECTOR_SIZE / sizeof(block_sector_t)) // 512 bytes / 4 bytes = 128 entries
/*1st-level indirect inode data block, storing an array of sectors containing inode data blocks.*/
typedef struct {
  block_sector_t data_blocks[INDIRECT_BLOCK_NUM_ENTRIES];
} inode_indirect_block_1_t;
#define INDIRECT_BLOCK_1_CAPACITY_BYTE (INDIRECT_BLOCK_NUM_ENTRIES * BLOCK_SECTOR_SIZE) // 128 * 512 = 65536 bytes = 64 KB
#define INDIRECT_BLOCK_1_CAPACITY_ENTRY INDIRECT_BLOCK_NUM_ENTRIES // mapped to 128 entries

/*2nd-level indirect inode data block, storing an array of sectors containing 1st level indirect data blocks.*/
typedef struct {
  block_sector_t l1_blocks[INDIRECT_BLOCK_NUM_ENTRIES];
} inode_indirect_block_2_t;
#define INDIRECT_BLOCK_2_CAPACITY_BYTE (INDIRECT_BLOCK_1_CAPACITY_BYTE * INDIRECT_BLOCK_NUM_ENTRIES) // 65536 * 128 = 8388608 bytes = 8 MiB
#define INDIRECT_BLOCK_2_CAPACITY_ENTRY (INDIRECT_BLOCK_NUM_ENTRIES * INDIRECT_BLOCK_1_CAPACITY_ENTRY) // mapped to 128 * 128 = 16384 entries
#define INODE_DISK_NUM_IDIRECT_BLOCKS_2 32 

#define INODE_DISK_NUM_DIRECT_BLOCKS (BLOCK_SECTOR_SIZE \
  - INODE_DISK_NUM_IDIRECT_BLOCKS_2 * sizeof(block_sector_t) \
  - sizeof(bool) - sizeof(off_t) - sizeof(unsigned)) / sizeof(block_sector_t)

#define INODE_DISK_NUM_DIRECT_BLOCKS_CAPACITY_BYTE (INODE_DISK_NUM_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)

struct inode_data {
  bool is_dir;         /* true if this inode corresponds a directory*/
  off_t size;         /* File size in bytes. */
  block_sector_t l2_blocks[INODE_DISK_NUM_IDIRECT_BLOCKS_2]; /* 2nd level indirect sector*/
  block_sector_t l0_blocks[INODE_DISK_NUM_DIRECT_BLOCKS]; /* directy sectors*/
};

struct inode_disk {
  #define INODE_DISK_PADDING \
  BLOCK_SECTOR_SIZE \
  - sizeof(struct inode_data) \
  - sizeof(unsigned) //magic number
  struct inode_data block_data;
  unsigned magic;       /* Magic number. */ // 4 bytes
  char padding[INODE_DISK_PADDING];
};


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct lock mtx_0;        /* Lock for metadatas.*/
  rwLock deny_write_cnt_lock; /* Lock for deny_write_cnt */
  rwLock size_lock; /* Lock for size, used when resizing during read()*/
  /* Data fetched from inode_disk*/
  struct inode_data block_data;
};

static void inode_writeback(struct inode *inode) {
  struct inode_disk disk_inode;
  disk_inode.block_data = inode->block_data;
  disk_inode.magic = INODE_MAGIC;
  memset(disk_inode.padding, 0, INODE_DISK_PADDING);
  FS_WRITE_BLOCK(&disk_inode, inode->sector);
}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);
  if (pos > inode->block_data.size) {
    return -1;
  }

  if (pos < INODE_DISK_NUM_DIRECT_BLOCKS_CAPACITY_BYTE) {
    return inode->block_data.l0_blocks[pos / BLOCK_SECTOR_SIZE];
  }

  pos -= INODE_DISK_NUM_DIRECT_BLOCKS_CAPACITY_BYTE;
  // which level 2 block?
  int l2_block_idx = pos / INDIRECT_BLOCK_2_CAPACITY_BYTE; // pos / 8 MiB; 0 or 1(for now)
  // which level 1 block?
  int l1_block_idx = (pos % INDIRECT_BLOCK_2_CAPACITY_BYTE) / INDIRECT_BLOCK_1_CAPACITY_BYTE;
  // which data block?
  int l0_block_idx = (pos % INDIRECT_BLOCK_1_CAPACITY_BYTE) / BLOCK_SECTOR_SIZE;
  // read the level 2 block
  block_sector_t indirect_block_2_sector = inode->block_data.l2_blocks[l2_block_idx];
  block_sector_t indirect_block_1_sector;
  buffer_cache_read(fs_buffer_cache, indirect_block_2_sector, &indirect_block_1_sector, l1_block_idx * sizeof(block_sector_t), sizeof(block_sector_t));
  block_sector_t data_block_sector;
  buffer_cache_read(fs_buffer_cache, indirect_block_1_sector, &data_block_sector, l0_block_idx * sizeof(block_sector_t), sizeof(block_sector_t));
  return data_block_sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_mtx;

static bool inode_data_resize(struct inode_data* a_inode_data, size_t a_size);

/* Initializes the inode module. */
void inode_init(void) { 
  list_init(&open_inodes); 
  lock_init(&open_inodes_mtx);
#if ENABLE_BUFFER_CACHE
  fs_buffer_cache = buffer_cache_create(fs_device);
  if (fs_buffer_cache == NULL) {
    PANIC("Failed to create inode buffer cache");
  }
#endif
  INFO2("Dumping inode data");
  INFO("Size of inode_disk = %d", sizeof(struct inode_disk));
  INFO("# of direct blocks = %d", INODE_DISK_NUM_DIRECT_BLOCKS);
  INFO("maximum direct block capacity = %d kilobytes", INODE_DISK_NUM_DIRECT_BLOCKS_CAPACITY_BYTE / 1024);
  INFO("# of 2nd-level indirect blocks = %d", INODE_DISK_NUM_IDIRECT_BLOCKS_2);
  INFO("maximum 2nd-level indirect block capacity = %d megabytes", INDIRECT_BLOCK_2_CAPACITY_BYTE * INODE_DISK_NUM_IDIRECT_BLOCKS_2 / 1024 / 1024);
  INFO("size of disk node padding = %d", INODE_DISK_PADDING);
  INFO2("Dumping inode data");
}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_directory) { //sector is allocated beforehand.
  bool success = false;

  ASSERT(length >= 0);

  ASSERT(sizeof (struct inode_disk) == BLOCK_SECTOR_SIZE);

  struct inode_disk disk_inode;
  disk_inode.block_data.size = 0;
  disk_inode.block_data.is_dir = is_directory;
  disk_inode.magic = INODE_MAGIC;
  success = inode_data_resize(&disk_inode.block_data, length);// no lock is needed here since the inode is not yet in the inode list
  if (success) {
    FS_WRITE_BLOCK(&disk_inode, sector);
  }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */  
struct inode* inode_open(block_sector_t sector) {
  
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_mtx); // lock read&write to open_inodes_list.
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&open_inodes_mtx);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->mtx_0);
  rwLock_init(&inode->deny_write_cnt_lock);
  rwLock_init(&inode->size_lock);

  struct inode_disk buf;
  FS_READ_BLOCK(inode->sector, &buf);
  inode->block_data = buf.block_data;
  
  lock_release(&open_inodes_mtx);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->mtx_0);
    inode->open_cnt++;
    lock_release(&inode->mtx_0);
  }
  return inode;
}

/* Returns INODE's inode number(disk_node sector number). */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

// 
/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  
  lock_acquire(&inode->mtx_0);
  --inode->open_cnt;
  int open_cnt = inode->open_cnt;
  lock_release(&inode->mtx_0);

  /* Release resources if this was the last opener. */
  if (open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);
    /* Deallocate blocks if removed. */
    if (inode->removed) { 
      int num_l0 = bytes_to_sectors(inode->block_data.size);
      ASSERT(num_l0 >= 0);

      int index_l2 = 0; int index_l1 = 0; int index_l0 = 0;//local index
      int i = 0; //total number of blocks freed

      /* Free direct blocks. */
      while (i < num_l0 && i < INODE_DISK_NUM_DIRECT_BLOCKS) {
        free_map_release(inode->block_data.l0_blocks[i], 1);
        i++;
      }

      /* Free 2nd level indirect blocks. */
      bool done = num_l0 == i;
      while (!done) { // i == num_10 means all l0 blocks are freed and we are done.
        inode_indirect_block_2_t indirect_block_2;
        FS_READ_BLOCK(inode->block_data.l2_blocks[index_l2], &indirect_block_2)
        while (index_l1 < INDIRECT_BLOCK_NUM_ENTRIES && !done) {
          inode_indirect_block_1_t indirect_block_1;
          FS_READ_BLOCK(indirect_block_2.l1_blocks[index_l1], &indirect_block_1)
          while (index_l0 < INDIRECT_BLOCK_NUM_ENTRIES && !done) {
            free_map_release(indirect_block_1.data_blocks[index_l0], 1);
            index_l0++;
            i++;
            done = i == num_l0;
          }
          free_map_release(indirect_block_2.l1_blocks[index_l1], 1); // l1 block freed after all l0 blocks are freed
          index_l0 = 0;
          index_l1++;
        }
        free_map_release(inode->block_data.l2_blocks[index_l2], 1); // l2 block freed after all l1 blocks are freed
        index_l1 = 0;
        index_l2++;
      }
      free_map_release(inode->sector, 1); // release disk inode
    }
    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->mtx_0);
  inode->removed = true;
  lock_release(&inode->mtx_0);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  off_t inode_size = inode_length(inode);

  if (inode_size < offset + size) { //don't read anything past EOF
    return 0;
  }

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    if (sector_idx == (block_sector_t)-1) {
      break;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_size - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

#if ENABLE_BUFFER_CACHE
    buffer_cache_read(fs_buffer_cache, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
#else
    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      block_read(fs_device, sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      block_read(fs_device, sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }
#endif
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
#if !ENABLE_BUFFER_CACHE
  free(bounce);
#endif
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;

  // resize
  wLock_acquire(&inode->size_lock);
  off_t required_size = offset + size;
  if (required_size > inode->block_data.size) {
    if (inode_data_resize(&inode->block_data, required_size)) { // update inode data
        inode_writeback(inode); // writeback inode data
    } else {
      wLock_release(&inode->size_lock);
      return;
    }
  }
  wLock_release(&inode->size_lock);

  //begin writing  
   // lock is held so other threads have to wait for this thread to finish writing before attempting deny.
   // using an rw allows multiple threads to still be able to write at the same time.
  rLock_acquire(&inode->deny_write_cnt_lock);
  if (inode->deny_write_cnt) { rLock_release(&inode->deny_write_cnt_lock); return 0; }

  rLock_acquire(&inode->size_lock); // block size can't be changed during read
  // write one sector(chunk) at a time.
  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset); //TODO: this introduces metadata reads that fails my custom test-2. Think of a workaround.
    if (sector_idx == (block_sector_t)-1) { // only possible when there isn't enough space to resize the inode for.
      break;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = MIN3(size, inode_left, sector_left);
    if (chunk_size <= 0) {
      break;
    }
#if ENABLE_BUFFER_CACHE
    buffer_cache_write(fs_buffer_cache, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
#else
    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      block_write(fs_device, sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        block_read(fs_device, sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      block_write(fs_device, sector_idx, bounce);
    }
#endif
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
#if !ENABLE_BUFFER_CACHE
  free(bounce);
#endif
  rLock_release(&inode->size_lock);
  rLock_release(&inode->deny_write_cnt_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  wLock_acquire(&inode->deny_write_cnt_lock);
  inode->deny_write_cnt++;
  wLock_release(&inode->deny_write_cnt_lock);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  wLock_acquire(&inode->deny_write_cnt_lock);
  inode->deny_write_cnt--;
  wLock_release(&inode->deny_write_cnt_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  rLock_acquire(&inode->size_lock);
  off_t ret = inode->block_data.size;
  rLock_release(&inode->size_lock);
  return ret; 
}

static struct new_sector_elem {
  bool multi_lvl;
  block_sector_t sector;
  int data_block_idx;
  int l1_block_idx;
  int l2_block_idx;
  struct list_elem elem;
} new_sector_elem;

static inline void zero_out(block_sector_t sector) {
  static const char zeros[BLOCK_SECTOR_SIZE];
  FS_WRITE_BLOCK(zeros, sector);
}

/* API function for other modules to resizes an inode. Not used by inode.*/
bool inode_resize(struct inode* a_inode, size_t a_size) {
  wLock_acquire(&a_inode->size_lock);
  bool ret = inode_data_resize(&a_inode->block_data, a_size);
  if (ret == true) {
    struct inode_disk disk_inode;
    inode_writeback(a_inode);
  }
  wLock_release(&a_inode->size_lock);
  return ret;
}

/* Resize A_INODE_DATA to A_SIZE, return FALSE if there is a disk space or memory shortage.*/
static bool inode_data_resize(struct inode_data* a_inode_data, size_t a_size) {
  ASSERT(a_inode_data->size <= a_size); // only support growing file size.
  if (a_inode_data->size == a_size) {
    return true; // nothing to do.
  }

  bool success = false;  
  struct list new_sectors;
  list_init(&new_sectors);

  size_t num_new_sectors = bytes_to_sectors(a_size);
  size_t num_old_sectors = bytes_to_sectors(a_inode_data->size);
  
  if (num_new_sectors == num_old_sectors) { // no need to resize. also this should never happen since the caller checks for the size
    success = true;
    goto done;
  }


  for (int i = num_old_sectors; i < num_new_sectors; i++) { //allocate disk space for new sectors
    struct new_sector_elem* new_sector = malloc(sizeof(struct new_sector_elem));
    if (new_sector == NULL) { // memory shortage
      success = false;
      goto done;
    }
    if (!free_map_allocate(1, &new_sector->sector)) { // disk space shortage
      free(new_sector);
      success = false;
      goto done;
    }
    zero_out(new_sector->sector); // zero out the new sector per convention

    new_sector->multi_lvl = i >= INODE_DISK_NUM_DIRECT_BLOCKS;
    //Calculate index
    if (new_sector->multi_lvl) {
      int j = i - INODE_DISK_NUM_DIRECT_BLOCKS;
      new_sector->l2_block_idx = j / INDIRECT_BLOCK_2_CAPACITY_ENTRY; // which l2 block
      new_sector->l1_block_idx = (j % INDIRECT_BLOCK_2_CAPACITY_ENTRY) / INDIRECT_BLOCK_1_CAPACITY_ENTRY; // which l1 block
      new_sector->data_block_idx = j % INDIRECT_BLOCK_1_CAPACITY_ENTRY; // which data block
    } else {
      new_sector->data_block_idx = i;
    }
    list_push_back(&new_sectors, &new_sector->elem);
  }

  // log new sectors to disk
  for (struct list_elem* e = list_begin(&new_sectors); e != list_end(&new_sectors); e = list_next(e)) {
    struct new_sector_elem* new_sector = list_entry(e, struct new_sector_elem, elem);
    int l1_block_idx = new_sector->l1_block_idx;
    int l2_block_idx = new_sector->l2_block_idx;
    int data_block_idx = new_sector->data_block_idx;
    
    if (new_sector->multi_lvl) {
      if (l1_block_idx == 0 && new_sector->data_block_idx == 0) { //need to allocate a new l2 block
        block_sector_t l2_block_sector;
        if (!free_map_allocate(1, &l2_block_sector)) { // disk space shortage
          success = false;
          break;
        }
        a_inode_data->l2_blocks[l2_block_idx] = l2_block_sector;
      }
      if (data_block_idx == 0) { //need to allocate a new l1 block
        block_sector_t l1_block_sector;
        if (!free_map_allocate(1, &l1_block_sector)) { // disk space shortage
          success = false;
          break;
        }
        buffer_cache_write(fs_buffer_cache, a_inode_data->l2_blocks[l2_block_idx], &l1_block_sector, l1_block_idx * sizeof(block_sector_t), sizeof(block_sector_t));
      }    
      //find the l1 block
      block_sector_t l1_block_sector;
      buffer_cache_read(fs_buffer_cache, a_inode_data->l2_blocks[l2_block_idx], &l1_block_sector, l1_block_idx * sizeof(block_sector_t), sizeof(block_sector_t));
      //write the new sector into the l1 block
      buffer_cache_write(fs_buffer_cache, l1_block_sector, &new_sector->sector, data_block_idx * sizeof(block_sector_t), sizeof(block_sector_t));
    } else {
      a_inode_data->l0_blocks[data_block_idx] = new_sector->sector;
    }
  }

  success = true;
done:
  //clean up
  while (!list_empty(&new_sectors)) {
    struct list_elem* e = list_pop_front(&new_sectors);
    struct new_sector_elem* new_sector = list_entry(e, struct new_sector_elem, elem);
    if (!success) {
      free_map_release(new_sector->sector, 1);
    }
    free(new_sector);
  }
  if (success) {
    a_inode_data->size = a_size;
  } 
  return success;
}

bool inode_is_dir(struct inode* a_inode) {
  return a_inode->block_data.is_dir;
}

int inode_get_open_cnt(struct inode* a_inode) {
  ASSERT(a_inode != NULL);
  return a_inode->open_cnt;
}