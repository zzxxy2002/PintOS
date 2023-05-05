#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* Opening and closing directories. */
bool dir_create(block_sector_t sector, size_t entry_cnt);
struct dir* dir_open(struct inode*);
struct dir* dir_open_root(void);
struct dir* dir_reopen(struct dir*);
void dir_close(struct dir*);
struct inode* dir_get_inode(struct dir*);

/* Reading and writing. */
enum DIR_LOOKUP_RESULT {
  DIR_LOOKUP_FOUND_DIR,
  DIR_LOOKUP_FOUND_FILE,
  DIR_LOOKUP_NOT_FOUND
};

enum DIR_LOOKUP_RESULT dir_lookup(const struct dir*, const char* name, struct inode**);
struct dir* dir_get_parent_dir_by_name(const char* a_name, char** a_fileNameRet);

bool dir_add(struct dir*, const char* name, block_sector_t);
bool dir_remove(struct dir*, const char* name);
bool dir_readdir(struct dir*, char name[NAME_MAX + 1]);
bool dir_mkdir(const char* a_name);
size_t dir_get_active_entries(struct dir* a_dir);
bool dir_is_empty(struct dir* a_dir);

size_t dir_get_size(struct dir* a_dir);
bool dir_resize(struct dir* a_dir, size_t entry_cnt);

bool dir_is_root(struct dir* a_dir);

void dir_set_pos(struct dir* a_dir, int32_t a_pos);
int32_t dir_get_pos(struct dir* a_dir);
#endif /* filesys/directory.h */
