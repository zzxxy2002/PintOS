#include "syscall_fp.h"
#include "lib/float.h"
bool syscall_compute_e_h(int a_in, void** a_ret, struct intr_frame* f UNUSED) {
    int ret = sys_sum_to_e(a_in);
    hRET(ret)
}