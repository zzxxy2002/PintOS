#pragma once
#include <stdio.h>
#include "threads/interrupt.h"
bool syscall_practice_h(int a_in, void** a_ret, struct intr_frame* f UNUSED);

bool syscall_halt_h(void** a_ret, struct intr_frame* f UNUSED);

bool syscall_exit_h(int a_status, void** a_ret, struct intr_frame* f UNUSED);

bool syscall_exec_h(const char* a_cmd_line, void** a_ret, struct intr_frame* f UNUSED);

bool syscall_wait_h(int a_pid, void** a_ret, struct intr_frame* f UNUSED);