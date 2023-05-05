
#pragma once
#define FPU_SIZE 108 /*Size of FPU register, in bytes*/
#define FPU_ENABLE 1

typedef struct fpu {
   unsigned char regs[FPU_SIZE]; 
} fpu_t; //a wrapper for fpu type

void fpu_init();

void fpu_save_initial_state(fpu_t* a_fpu);