#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include "threads/interrupt.h"

void syscall_init(void);

#endif /* userprog/syscall.h */

/*Utilities*/
/*save return value to a_ret and return 1*/
#define hRET(val)                                                                                  \
  *a_ret = (void*)val;                                                                             \
  return 1;
