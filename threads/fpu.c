#include "fpu.h"

void fpu_init(void) {
  asm volatile("fninit");
}


/**
 * @brief Save the initial state of FPU into the given FPU struct.
 * 
 * @param a_fpu 
 */
void fpu_save_initial_state(fpu_t* a_fpu) {
  fpu_t temp;
  asm volatile("fsave %0"::"m"(temp.regs));//preserve the current state of FPU
  asm volatile("fninit");//fninit
  asm volatile("fsave %0"::"m"(a_fpu->regs));//save the initial state of FPU
  asm volatile("frstor %0"::"m"(temp.regs));//restore the current state of FPU
}
