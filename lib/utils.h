#pragma once
#include <stdio.h>
#include "userprog/process.h"
/*Bunch of useful functions*/
#define nullptr NULL

#define PRINTDEBUGMSG 0
#define PRINTINFO 0

/*Toggleably prints a debug message as well as the current thread it printing it. Automatically prints endl.*/
#define DEBUG(...) \
if (PRINTDEBUGMSG) {printf("[thread %i]", thread_current()->tid); printf(__VA_ARGS__); printf("\n");}


/*Toggleably prints a info message as well as the current thread it printing it. Automatically prints endl.*/
#define INFO(...) \
if (PRINTINFO) {printf("[INFO]"); printf(__VA_ARGS__); printf("\n");}
#define INFO2(...) \
if (PRINTINFO) {printf("===================="); printf(__VA_ARGS__); printf("====================\n");}

void* push(void* a_ptr, size_t a_size, void** a_esp);

#define VALID(ptr) is_valid_user_ptr(ptr)
#define VALIDC(ptr) is_valid_user_char_ptr(ptr)
#define VALIDS(ptr, size) is_valid_user_memory_section(ptr, size)
bool is_valid_user_ptr(const void* a_ptr);
bool is_valid_user_char_ptr(const char* a_ptr);
bool is_valid_user_memory_section(const void* a_ptr, size_t a_size);
struct process* get_running_pcb();
pid_t get_running_pid();
pid_t get_pid(struct process* a_pcb);
char* get_proc_name(struct process* a_pcb);


bool is_driver_process_running();

/*Lock for more efficient reads and writes*/
typedef struct {
	struct lock mtx_global;
	struct lock mtx_readers;
	int readers;
} rwLock;

void rwLock_init(rwLock* a_lock);

void rLock_acquire(rwLock* a_lock);
void rLock_release(rwLock* a_lock);
void wLock_acquire(rwLock* a_lock);
void wLock_release(rwLock* a_lock);


#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN3(a,b,c) MIN(MIN(a,b),c)
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#pragma endregion