#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "utils.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) { 
  free_map_close();
#if ENABLE_BUFFER_CACHE
    buffer_cache_flush(fs_buffer_cache);
#endif

}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* a_full_name, off_t initial_size) {
  block_sector_t inode_sector = 0;
  char* file_name = NULL;
  struct dir* dir = dir_get_parent_dir_by_name(a_full_name, &file_name);
  bool success = false;
  success = dir != NULL;
  if (success) {
    success = free_map_allocate(1, &inode_sector);
  }
  if (success) {
    success = inode_create(inode_sector, initial_size, false);
  }
  if (success) {
    success = dir_add(dir, file_name, inode_sector);
  }
  if (!success && inode_sector != 0) {
    free_map_release(inode_sector, 1);
  }
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* a_full_name) {
  struct inode* inode = filesys_search(a_full_name, FILESYS_SEARCH_TYPE_ANY);
  return inode ? file_open(inode) : NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* full_name) {
  bool success = false;
  char* real_name = NULL;
  struct dir* dir = dir_get_parent_dir_by_name(full_name, &real_name);
  if (dir != NULL) {
    success = dir_remove(dir, real_name);
    dir_close(dir);
  }
  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}

/* Search for the file with A_FULL_NAME and return its inode. A_FULL_NAME can be either absolute or relative path.
  Return NULL if type don't match or not found.
  special case: if A_FULL_NAMAE is '/', return the root directory inode.  
*/
struct inode* filesys_search(const char* a_full_name, enum filesys_search_type a_type) {
  if (strcmp(a_full_name, "/") == 0) { // root dir
    return inode_open(ROOT_DIR_SECTOR);
  }
  struct inode* inode = NULL;
  bool found = false; 
  bool is_directory = false;
  // search absolute address
  struct dir* root_dir = dir_open_root();
  if (root_dir != NULL) {
    enum DIR_LOOKUP_RESULT res = dir_lookup(root_dir, a_full_name, &inode);
    if (res != DIR_LOOKUP_NOT_FOUND) {
      found = true;
      is_directory = res == DIR_LOOKUP_FOUND_DIR;
    }
  }
  dir_close(root_dir);

  // search CWD
  if (!found) {
    struct dir* cwd = get_running_pcb() ? get_running_pcb()->cwd : NULL;
    if (cwd != NULL && !dir_is_root(cwd)) { // don't search root again
      enum DIR_LOOKUP_RESULT res = dir_lookup(cwd, a_full_name, &inode);
      if (res != DIR_LOOKUP_NOT_FOUND) {
        found = true;
        is_directory = res == DIR_LOOKUP_FOUND_DIR;
      }
    }
  }

  if (found) {
    switch (a_type) {
    case FILESYS_SEARCH_TYPE_FILE:
      if (is_directory) {
        inode_close(inode);
        return NULL;
      }
      break;
    case FILESYS_SEARCH_TYPE_DIR:
      if (!is_directory) {
        inode_close(inode);
        return NULL;
      }
      break;
    case FILESYS_SEARCH_TYPE_ANY:
      break;
    }
  }

  return inode;

}