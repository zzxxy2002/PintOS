#include "syscall_file.h"
#include "userprog/syscall.h"
#include "threads/synch.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "process.h"
#include "lib/utils.h"
#include "filesys/inode.h"

/* Global filesys locks, only enabled when buffer_cache(which implements its own synchronization) is not in use. */
#define LOCK() if (!ENABLE_BUFFER_CACHE) {lock_acquire(&hackyLock);}
#define UNLOCK() if (!ENABLE_BUFFER_CACHE) {lock_release(&hackyLock);}

bool isFileProtected(const char* a_file) {
  return process_is_active_name(a_file);
}

/**Invoked when OS boots. Initialize all static data needed by syscall_fileHandler.*/
void syscall_fileHandler_init() {
  if (!ENABLE_BUFFER_CACHE) {
    lock_init(&hackyLock);
  }
}

bool syscall_create_h(const char* a_file, unsigned a_initial_size, void** a_ret,
                      struct intr_frame* f UNUSED) {
  if (!VALIDC(a_file)) {
    return false;
  }
  bool success;
  LOCK();
  success = filesys_create(a_file, a_initial_size);
  UNLOCK();
  hRET(success)
}

bool syscall_remove_h(const char* a_file, void** a_ret, struct intr_frame* f UNUSED) {
  if (!VALIDC(a_file)) {
    return false;
  }
  bool success;
  LOCK();
  success = filesys_remove(a_file);
  UNLOCK();
  hRET(success)
}

bool syscall_open_h(const char* a_file, void** a_ret, struct intr_frame* f UNUSED) {
  if (!VALIDC(a_file)) {
    return false;
  }
  struct file* file;
  int res;
  LOCK();
  file = filesys_open(a_file);
  if (file == NULL) {
    UNLOCK();
    hRET(-1) /*return -1 if the file cann't be opened*/
  }
  res = process_fd_open(get_running_pcb(), file, a_file);
  UNLOCK();
  hRET(res)
}

bool syscall_filesize_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED) {
  LOCK();
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    UNLOCK();
    hRET(-1)
  }
  struct file* file = fd->file;
  int res = file_length(file);
  UNLOCK();
  hRET(res)
}

bool syscall_read_h(int a_fd, void* a_buffer, unsigned a_size, void** a_ret,
                    struct intr_frame* f UNUSED) {
  if (!VALIDS(a_buffer, a_size)) {
    return false;
  }
  if (a_fd == 0) { /*read from keyboard*/
    size_t i;
    uint8_t* buffer = (uint8_t*)a_buffer;
    for (i = 0; i < a_size; i++) {
      buffer[i] = input_getc();
    }
    hRET(a_size)
  }

  LOCK();
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    UNLOCK();
    hRET(-1)
  }
  struct file* file = fd->file;
  if (inode_is_dir(file_get_inode(file))) { //deny read from directory
    UNLOCK();
    DEBUG("Denied read from directory %s\n", fd->file_name);
    hRET(-1)
  }

  int res;
  res = file_read(file, a_buffer, a_size);
  UNLOCK();
  hRET(res)
}

bool syscall_write_h(int a_fd, const void* a_buffer, size_t a_size, void** a_ret,
                     struct intr_frame* f UNUSED) {
  if (!VALIDS(a_buffer, a_size)) {
    return false;
  }

  if (a_fd == 1) { /*write to the console*/
    LOCK();
    putbuf(a_buffer, a_size);
    UNLOCK();
    hRET(a_size)
  }

  LOCK();
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    UNLOCK();
    hRET(-1)
  }
  if (isFileProtected(fd->file_name)) { /*protected from being written*/
    UNLOCK();
    hRET(0)
  }

  struct file* file = fd->file;
  if (inode_is_dir(file_get_inode(file))) {//deny write to directory
    UNLOCK();
    DEBUG("Denied write to directory %s\n", fd->file_name);
    hRET(-1)
  }

  int res = file_write(file, a_buffer, a_size);
  UNLOCK();
  hRET(res)
}

bool syscall_seek_h(int a_fd, unsigned a_position, void** a_ret, struct intr_frame* f UNUSED) {
  LOCK();
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    UNLOCK();
    return true; /*simply do nothing*/
  }
  struct file* file = fd->file;
  file_seek(file, a_position);
  UNLOCK();
  return true;
}

bool syscall_tell_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED) {

  LOCK();
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    UNLOCK();
    hRET(-1)
  }
  struct file* file = fd->file;
  int res;
  res = file_tell(file);
  UNLOCK();
  hRET(res)

  NOT_REACHED()
}

bool syscall_close_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED) {
  LOCK();
  int res;
  res = process_fd_close(get_running_pcb(), a_fd);
  if (res == -1) {
    UNLOCK();
    hRET(-1)
  }
  UNLOCK();
  return true;
}

bool syscall_filesys_get_read_write_count_h(unsigned long long* a_read_count, unsigned long long* a_write_count, void** a_ret, struct intr_frame* f UNUSED) {
  *a_read_count = block_get_read_cnt(fs_device);
  *a_write_count = block_get_write_cnt(fs_device);
  return true;
}

bool syscall_cache_get_hit_miss_time_h(int* a_hit_time, int* a_miss_time, void** a_ret, struct intr_frame* f UNUSED) {
#if ENABLE_BUFFER_CACHE
  *a_hit_time = buffer_cache_get_hit_time(fs_buffer_cache);
  *a_miss_time = buffer_cache_get_miss_time(fs_buffer_cache);
#else 
  *a_hit_time = -1;
  *a_miss_time = -1;
#endif
  return true;
}


bool syscall_cache_reset_h(void** a_ret, struct intr_frame* f UNUSED) {
#if ENABLE_BUFFER_CACHE
  buffer_cache_reset(fs_buffer_cache);
#endif
  return true;
}

bool syscall_chdir_h(const char* a_dir, void** a_ret, struct intr_frame* f UNUSED) {
  if (!VALIDC(a_dir)) {
    return false;
  }
  bool success = false;

  /* Search for new dir*/
  struct inode* inode = filesys_search(a_dir, FILESYS_SEARCH_TYPE_DIR);
  if (inode != NULL) {
    struct dir* new_dir = dir_open(inode);
    if (new_dir != NULL) {
      dir_close(get_running_pcb()->cwd);
      get_running_pcb()->cwd = new_dir;
      success = true;
    }
  }

  hRET(success)
}

bool syscall_mkdir_h(const char* a_dir, void** a_ret, struct intr_frame* f UNUSED) {
  if (!VALIDC(a_dir)) {
    return false;
  }
  if (strlen(a_dir) == 0) {
    hRET(0)
  }
  bool res = dir_mkdir(a_dir);
  hRET(res)
}
bool syscall_readdir_h(int a_fd, char* a_name, void** a_ret, struct intr_frame* f UNUSED) {
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  struct inode* inode = file_get_inode(fd->file);
  if (!inode_is_dir(inode)) {
    hRET(false)
  }
  struct dir* dir = dir_open(inode_reopen(inode));
  dir_set_pos(dir, file_tell(fd->file));
  bool res = dir_readdir(dir, a_name);
  file_seek(fd->file, dir_get_pos(dir));
  dir_close(dir);
  hRET(res);
}

bool syscall_isdir_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED) {
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    hRET(false)
  }

  hRET(inode_is_dir(file_get_inode(fd->file)))
}

bool syscall_inumber_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED) {
  struct L_fdt_elem* fd = process_fd_get(get_running_pcb(), a_fd);
  if (fd == NULL) {
    hRET(-1)
  }

  struct inode* inode = file_get_inode(fd->file);
  hRET(inode_get_inumber(inode))
}