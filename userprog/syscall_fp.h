#pragma once
#include "userprog/syscall.h"
#include <stdio.h>
bool syscall_compute_e_h(int a_in, void** a_ret, struct intr_frame* f UNUSED);