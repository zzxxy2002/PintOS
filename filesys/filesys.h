#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block* fs_device;

enum filesys_search_type {
	FILESYS_SEARCH_TYPE_FILE, //only return none-directory/
	FILESYS_SEARCH_TYPE_DIR, //only return directory
	FILESYS_SEARCH_TYPE_ANY //return both.
};	

struct inode* filesys_search(const char* a_full_name, enum filesys_search_type a_type);

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size);
struct file* filesys_open(const char* name);
bool filesys_remove(const char* name);

#endif /* filesys/filesys.h */
