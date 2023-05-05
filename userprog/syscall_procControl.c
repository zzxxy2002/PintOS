#include "syscall_procControl.h"
#include "syscall.h"
#include "threads/thread.h"
#include "process.h"
#include "lib/utils.h"

bool syscall_practice_h(int a_in, void** a_ret, struct intr_frame* f UNUSED) { hRET((a_in + 1)) }

bool syscall_halt_h(void** a_ret, struct intr_frame* f UNUSED) {
  shutdown_power_off();
  NOT_REACHED();
}

bool syscall_exit_h(int a_status, void** a_ret, struct intr_frame* f UNUSED) {
  f->eax = a_status;
  shared_data_update(get_running_pcb()->exit_status, a_status, get_running_pid());
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, a_status);
  process_exit();
  NOT_REACHED()
}

bool syscall_exec_h(const char* a_cmd_line, void** a_ret, struct intr_frame* f UNUSED) {
  if (!VALIDC(a_cmd_line)) {
    return false;
  }
  pid_t res = process_execute(a_cmd_line);
  if (res == TID_ERROR) {
    hRET(-1)
  }
  hRET(res);
}

bool syscall_wait_h(int a_pid, void** a_ret, struct intr_frame* f UNUSED) {
  int res = process_wait(a_pid);
  hRET(res)
}