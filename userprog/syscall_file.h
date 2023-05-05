#pragma once
#include <stdio.h>
#include "threads/synch.h"
#include "threads/interrupt.h"

//*A naive implementation of file system to make the grader happy. More complex version will be done in future projects. */

struct lock hackyLock; /*hacky global file lock for project 1*/

bool syscall_create_h(const char* a_file, unsigned a_initial_size, void** a_ret,
                      struct intr_frame* f UNUSED);
bool syscall_remove_h(const char* a_file, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_open_h(const char* a_file, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_filesize_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_read_h(int a_fd, void* a_buffer, unsigned a_size, void** a_ret,
                    struct intr_frame* f UNUSED);
bool syscall_write_h(int a_fd, const void* a_buffer, size_t a_size, void** a_ret,
                     struct intr_frame* f UNUSED);
bool syscall_seek_h(int a_fd, unsigned a_position, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_tell_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_close_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED);

void syscall_fileHandler_init();

// Project 4
bool syscall_filesys_get_read_write_count_h(unsigned long long* a_read_count, unsigned long long* a_write_count, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_cache_get_hit_miss_time_h(int* a_hit_time, int* a_miss_time, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_cache_reset_h(void** a_ret, struct intr_frame* f UNUSED);

bool syscall_chdir_h(const char* a_dir, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_mkdir_h(const char* a_dir, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_readdir_h(int a_fd, char* a_name, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_isdir_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED);
bool syscall_inumber_h(int a_fd, void** a_ret, struct intr_frame* f UNUSED);