#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "syscall_file.h"
#include "syscall_procControl.h"
#include "syscall_fp.h"
#include "lib/utils.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

/**
 * @brief Return from system call handler with a value.
 * @note this function may only be called if syscall does not encounter any explicit error.
 */
#define SYSCALL_RETURN(ret)                                                                        \
  f->eax = ret;                                                                                    \
  return;

/**
 * @brief Handle syscall error, such as invalid syscall number, invalid argument, etc.
 * Errors including: invalid syscall number, invalid argument, etc...
 * Currently handling terminates the process with exit code -1.
 * @note this function may only be called if syscall encounters an explicit error.
 */
#define SYSCALL_ERROR()                                                                            \
  syscall_exit_h(-1, NULL, f);                                                                     \
  NOT_REACHED()

/**
 * Macro to dispatch system call.
 */
#define DISPATCH_0ARG(handlerFunc)                                                                 \
  if (!handlerFunc(&ret, f)) {                                                                     \
    SYSCALL_ERROR()                                                                                \
  } else {                                                                                         \
    SYSCALL_RETURN(ret)                                                                            \
  }
#define DISPATCH_1ARG(handlerFunc)                                                                 \
  if (!VALIDS(args + 1, 4)) {                                                                      \
    SYSCALL_ERROR()                                                                                \
  }                                                                                                \
  if (!handlerFunc(args[1], &ret, f)) {                                                            \
    SYSCALL_ERROR()                                                                                \
  } else {                                                                                         \
    SYSCALL_RETURN(ret)                                                                            \
  }
#define DISPATCH_2ARG(handlerFunc)                                                                 \
  if (!VALIDS(args + 1, 8)) {                                                                      \
    SYSCALL_ERROR()                                                                                \
  }                                                                                                \
  if (!handlerFunc(args[1], args[2], &ret, f)) {                                                   \
    SYSCALL_ERROR()                                                                                \
  } else {                                                                                         \
    SYSCALL_RETURN(ret)                                                                            \
  }
#define DISPATCH_3ARG(handlerFunc)                                                                 \
  if (!VALIDS(args + 1, 12)) {                                                                     \
    SYSCALL_ERROR()                                                                                \
  }                                                                                                \
  if (!handlerFunc(args[1], args[2], args[3], &ret, f)) {                                          \
    SYSCALL_ERROR()                                                                                \
  } else {                                                                                         \
    SYSCALL_RETURN(ret)                                                                            \
  }

/**
 * @brief Handle a system call. Dispatch them to the corresponding subhandlers.
 * @note For a syscall handler subroutine to work, it must take all arguments of the original syscall,
 * plus a reference to the return value and interrupt frame. 
 * The handler returns true if processes the syscall successfully.
 * A false return from any syscall handler indicates an explicit error(e.g. invalid argument) and the process will be terminated.
 * Implicit errors(e.g. filesize() not finding the file) are handled by the handler itself, usually indicated by a negative return value.
 * All handler's functionalities can be found here:
 * https://cs162.org/static/proj/proj-userprog/docs/tasks/process-control-syscalls/
 * https://cs162.org/static/proj/proj-userprog/docs/tasks/file-operation-syscalls/
 * https://cs162.org/static/proj/proj-userprog/docs/tasks/floating-point-operations/
 * https://cs162.org/static/proj/proj-filesys/docs/tasks/subdirectories/
 * @param f The interrupt frame for the caller. 
 */
static void syscall_handler(struct intr_frame* f UNUSED) {

  uint32_t* args = ((uint32_t*)f->esp);

  // first 4 bytes and the following 12 bytes(3args * 4 bytes/arg) must be valid
  // bytes of arguments will be checked depending on the syscall numeber & corresponding # of arguments

  // check first 4 bytes
  if (!VALIDS(args, 4)) {
    SYSCALL_ERROR()
  }

  uint32_t syscall_num = args[0];

  void* ret = NULL;

  switch (syscall_num) {
    case SYS_PRACTICE:
      DISPATCH_1ARG(syscall_practice_h);
      break;
    case SYS_HALT:
      DISPATCH_0ARG(syscall_halt_h);
      break;
    case SYS_EXIT:
      DISPATCH_1ARG(syscall_exit_h);
      break;
    case SYS_EXEC:
      DISPATCH_1ARG(syscall_exec_h);
      break;
    case SYS_WAIT:
      DISPATCH_1ARG(syscall_wait_h);
      break;
    case SYS_CREATE:
      DISPATCH_2ARG(syscall_create_h);
      break;
    case SYS_REMOVE:
      DISPATCH_1ARG(syscall_remove_h);
      break;
    case SYS_OPEN:
      DISPATCH_1ARG(syscall_open_h);
      break;
    case SYS_FILESIZE:
      DISPATCH_1ARG(syscall_filesize_h);
      break;
    case SYS_READ:
      DISPATCH_3ARG(syscall_read_h);
      break;
    case SYS_WRITE:
      DISPATCH_3ARG(syscall_write_h);
      break;
    case SYS_SEEK:
      DISPATCH_2ARG(syscall_seek_h);
      break;
    case SYS_TELL:
      DISPATCH_1ARG(syscall_tell_h);
      break;
    case SYS_CLOSE:
      DISPATCH_1ARG(syscall_close_h);
      break;
    case SYS_COMPUTE_E:
      DISPATCH_1ARG(syscall_compute_e_h);
      break;
    case SYS_FILESYS_GET_READ_WRITE_COUNT:
      DISPATCH_2ARG(syscall_filesys_get_read_write_count_h);
      break;
    case SYS_CACHE_GET_HIT_MISS_TIME:
      DISPATCH_2ARG(syscall_cache_get_hit_miss_time_h);
      break;
    case SYS_CACHE_RESET:
      DISPATCH_0ARG(syscall_cache_reset_h);
    case SYS_CHDIR:
      DISPATCH_1ARG(syscall_chdir_h);
    case SYS_MKDIR:
      DISPATCH_1ARG(syscall_mkdir_h);
    case SYS_READDIR:
      DISPATCH_2ARG(syscall_readdir_h);
    case SYS_ISDIR:
      DISPATCH_1ARG(syscall_isdir_h);
    case SYS_INUMBER:
      DISPATCH_1ARG(syscall_inumber_h);
      break;
  }
}
