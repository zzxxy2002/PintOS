#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "lib/utils.h"

static const char name_cwd[2] = {'.', '\0'};
static const char name_prd[3] = {'.', '.', '\0'};

#define DIR_DEFAULT_SIZE 16 //default number of entries this directory can hold.
#define DIR_RESIZE_STEP 8 //number of entries to add when resizing.
/* A directory. */
struct dir {
  struct inode* inode; /* Backing store. */
  off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1];     /* Null terminated file name. */
  bool in_use;                 /* In use or free? */
};


/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt) {
  return inode_create(sector, entry_cnt * sizeof(struct dir_entry), true);
}


/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. 
   NOTE: since dir takes full ownership of inode, must open a new inode for each dir.
*/   
struct dir* dir_open(struct inode* inode) {
  struct dir* dir = calloc(1, sizeof *dir);
  if (inode != NULL && dir != NULL) {
    ASSERT(inode_is_dir(inode));
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  } else {
    inode_close(inode);
    free(dir);
    return NULL;
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir* dir_open_root(void) {
  return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir* dir_reopen(struct dir* dir) {
  return dir_open(inode_reopen(dir->inode));
}

/* Destroys DIR and frees associated resources. */
void dir_close(struct dir* dir) {
  if (dir != NULL) {
    inode_close(dir->inode);
    free(dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode* dir_get_inode(struct dir* dir) {
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir* dir, const char* name, struct dir_entry* ep, off_t* ofsp) {
  struct dir_entry e;
  size_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (e.in_use && !strcmp(name, e.name)) {
      if (ep != NULL)
        *ep = e;
      if (ofsp != NULL)
        *ofsp = ofs;
      return true;
    }
  return false;
}

static int get_next_part(char part[NAME_MAX + 1], const char** srcp);

/* Searches DIR for a file or dir with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE.*/
enum DIR_LOOKUP_RESULT dir_lookup(const struct dir* a_dir, const char* name_full, struct inode** inode) {
  struct dir_entry e;
  enum DIR_LOOKUP_RESULT ret;

  ASSERT(a_dir != NULL);
  ASSERT(name_full != NULL);

  *inode = NULL;

  struct inode* next = NULL;
  bool next_is_dir = false;
  char name_local[NAME_MAX + 1];

  struct dir* dir = dir_reopen(a_dir);
  if (dir == NULL) { ret = DIR_LOOKUP_NOT_FOUND; goto done; }
  while (1) {
    int res = get_next_part(name_local, &name_full);
    ASSERT(res != -1);
    if (res == 0) { // at end of file path.
      if (next) {
        *inode = inode_reopen(next); // must make a copy of inode, since next will be closed at cleanup.
        ret = next_is_dir ? DIR_LOOKUP_FOUND_DIR : DIR_LOOKUP_FOUND_FILE; goto done;
      } else {
        ret =  DIR_LOOKUP_NOT_FOUND; goto done;
      }
    } else { // name_local corresponds to a valid file
      if (next != NULL) {
        if (!next_is_dir) { // not at the end, but next is not a dir.
          ret = DIR_LOOKUP_NOT_FOUND; goto done;
        }
      }
    }
    if (!lookup(dir, name_local, &e, NULL)) { // entry not found
      ret = DIR_LOOKUP_NOT_FOUND; goto done;
    }
    if (next != NULL) {
      inode_close(next);// close previous inode
    }
    next = inode_open(e.inode_sector);
    if (next == NULL) {
      ret = DIR_LOOKUP_NOT_FOUND; goto done;
    }
    if (inode_is_dir(next)) { // directory
      next_is_dir = true;
      /* inode_reopen is called here because DIR will be closed by the end of call, 
        whereas NEXT might be the return value. */
      struct inode* next_dir_inode = inode_reopen(next);
      dir_close(dir);
      dir = dir_open(next_dir_inode); // open directory and keeps traversal
      ASSERT(dir != NULL);
    }
  }
  
done:
  if (next != NULL) {
    inode_close(next);
  }
  if (dir != NULL) {
    dir_close(dir);
  }
  switch (ret) {
    case DIR_LOOKUP_FOUND_FILE:
      break;
    case DIR_LOOKUP_FOUND_DIR:
      break;
    case DIR_LOOKUP_NOT_FOUND:
      break;
  }
  return ret;
}

/* Adds a file/directory named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool dir_add(struct dir* dir, const char* name, block_sector_t inode_sector) {
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen(name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup(dir, name, NULL, NULL))
    goto done;

  bool found_slot = false;
  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
    if (!e.in_use) {
      found_slot = true;
      break;
    }
  }
  if (!found_slot) { //offset is now at the end of directory, extending the dir allows us to add a new entry.
    if(!dir_resize(dir, dir_get_size(dir) + DIR_RESIZE_STEP)) {
      goto done;
    }
  }
  /* Write slot. */
  e.in_use = true;
  strlcpy(e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir* dir, const char* name) {
  struct dir_entry e;
  struct inode* inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT(dir != NULL);
  ASSERT(name != NULL);

  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open(e.inode_sector);
  if (inode == NULL)
    goto done;
  
  /* For directories */
  if (inode_is_dir(inode)) {
    struct dir* dir_to_remove = dir_open(inode); // make a temp dir to check if it is empty
    bool can_remove = dir_is_empty(dir_to_remove) && !dir_is_root(dir_to_remove) && inode_get_open_cnt(inode) == 1;
    free(dir_to_remove); 
    if (!can_remove) {
      goto done;
    }
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at(dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove(inode);
  success = true;

done:
  if (inode != NULL) { inode_close(inode);}
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool dir_readdir(struct dir* dir, char name[NAME_MAX + 1]) {
  struct dir_entry e;

  while (inode_read_at(dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
    dir->pos += sizeof e;
    if (e.in_use) {
      if (strcmp(e.name, name_cwd) == 0 || strcmp(e.name, name_prd) == 0) { // ignore . and ..
        continue;
      }
      strlcpy(name, e.name, NAME_MAX + 1);
      return true;
    }
  }
  return false;
}

/*Find the entry of A_INODE stored in A_DIR. Return NULL if A_DIR doesn't store A_INODE.*/
static struct dir_entry* dir_find_entry(struct dir* a_dir, struct inode* a_inode) {
  struct dir_entry e;
  size_t ofs;
  block_sector_t inode_number = inode_get_inumber(a_inode);
  ASSERT(a_dir != NULL);
  ASSERT(a_inode != NULL);
  for (ofs = 0; inode_read_at(a_dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
    if (e.in_use && e.inode_sector == inode_number) {
      return &e;
    }
  }
  return NULL;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Search and returns NAME's upper directory. Name does not need to be a valid file. Defaults to CWD if the name does not contain an upper directory.
  If A_FILENAME is not NULL, set it to the local file name. A_FILENAMERET is an pointer advanced from A_NAME that points to the local file name.
  Example: "\file_name.txt" => return CWD, save ptr to file_name.txt in A_FILENAME
            "dir\dir2\file_name.txt" => return dir2, save ptr to file_name.txt in A_FILENAME
*/
struct dir* dir_get_parent_dir_by_name(const char* a_name, char** a_fileNameRet) {
  struct dir* ret = NULL;

  size_t name_len = strlen(a_name);
  ASSERT(a_name[name_len] != '/'); // name must not end with a slash

  char* name = malloc(name_len + 1);
  if (name==NULL) {goto done;}
  strlcpy(name, a_name, name_len + 1); // make a copy of name

  bool found_upper_name = false;
  char* local_name = a_name;
  for (int i = name_len - 2; i >= 0; i--) {
    if (name[i] == '/') {// find the last '/', everything before it is the upper directory
      local_name = a_name + i + 1;
      if (i - 1 >= 0 && name[i - 1] != '/') {
        name[i] = '\0';
        found_upper_name = true;
        break;
      }
    }
  }

  if (!found_upper_name) { // no upper directory
    struct dir* cwd = get_running_pcb()->cwd;
    ret = cwd ? dir_reopen(cwd) : dir_open_root();
    goto done;
  }


  struct inode* res = filesys_search(name, FILESYS_SEARCH_TYPE_DIR);
  if (res != NULL) {
    ret = dir_open(res);
  }
  
done:
  if (name != NULL) {
    free(name);
  }
  if (ret != NULL) {
    // save the local file name
    if (a_fileNameRet != NULL) {
      *a_fileNameRet = local_name;
    }
  } else {
  }
  return ret;
}

bool dir_mkdir(const char* a_full_name) {
  char* dir_name = NULL;
  struct dir* new_dir = NULL;
  struct dir* parent_dir = dir_get_parent_dir_by_name(a_full_name, &dir_name);

  if (parent_dir == NULL) {
    return false;
  }

  bool success = false;
  block_sector_t new_dir_sector = 0; // sector of the new directory
  if (!free_map_allocate(1, &new_dir_sector)) { //allocate a free map.
    dir_close(parent_dir);
    return false;
  }
  if (!dir_create(new_dir_sector, DIR_DEFAULT_SIZE)) {
    goto cleanup;
  }
  if (!dir_add(parent_dir, dir_name, new_dir_sector)) {
    goto cleanup;
  }
  // add "." and ".." to the new directory
  new_dir = dir_open(inode_open(new_dir_sector));
  if (new_dir == NULL) {
    goto cleanup;
  }
  if (!dir_add(new_dir, ".", new_dir_sector)) {
    goto cleanup;
  }
  block_sector_t parent_sector = inode_get_inumber(parent_dir->inode);
  if (!dir_add(new_dir, "..", parent_sector)) {
    goto cleanup;
  }
  success = true;
cleanup:
  dir_close(parent_dir); //parent_dir guaranteed to be not NULL.
  if (new_dir != NULL) {
    dir_close(new_dir);
  }
  if (success) {
  } else {
    free_map_release(new_dir_sector, 1);
  }
  return success;
}

/* Return the number of entries this directory can contain.*/
size_t dir_get_size(struct dir* a_dir) {
  ASSERT(a_dir != NULL);
  ASSERT(a_dir->inode != NULL);
  return inode_length(a_dir->inode) / sizeof(struct dir_entry); // inode length is always multiple of sizeof(struct dir_entry)
}

/* Return # of files&subdirectories this directory contains, ignoring "." and ".." directory.*/
size_t dir_get_active_entries(struct dir* a_dir) {
  ASSERT(a_dir != NULL);
  ASSERT(a_dir->inode != NULL);
  struct dir_entry e;
  size_t ofs;
  size_t count = 0;
  for (ofs = 0; inode_read_at(a_dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e) {
    if (e.in_use && strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0) {
      count++;
    }
  }
  return count;
}

bool dir_resize(struct dir* a_dir, size_t entry_cnt) {
  ASSERT(a_dir != NULL);
  ASSERT(dir_get_size(a_dir) <= entry_cnt);
  return inode_resize(a_dir->inode, entry_cnt * sizeof(struct dir_entry));
}

bool dir_is_root(struct dir* a_dir) {
  ASSERT(a_dir != NULL);
  return inode_get_inumber(a_dir->inode) == ROOT_DIR_SECTOR;
}

bool dir_is_empty(struct dir* a_dir) {
  ASSERT(a_dir != NULL);
  return dir_get_active_entries(a_dir) == 0;
}

void dir_set_pos(struct dir* a_dir, off_t a_pos) {
  ASSERT(a_dir != NULL);
  a_dir->pos = a_pos;
}

off_t dir_get_pos(struct dir* a_dir) {
  ASSERT(a_dir != NULL);
  return a_dir->pos;
}