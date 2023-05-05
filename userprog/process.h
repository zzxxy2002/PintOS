#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>
#include "syscall_file.h" //TODO:remove this hacky dependency in future projects when file is properly implemented.
#include "userprog/custom_lists.h"
#include <shared_data.h>
#include "filesys/directory.h"


// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127
#define MAX_FILE_NAME 16 /*Maximum length of a proc's file name to be recorded.*/

#define MAIN_PROC_ID 0 /*Main proc's id*/

L_activeProcs active_procs; /*List of active processes*/

bool process_is_active_name(char* a_name);
/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);


int process_fd_open(struct process* a_pcb, struct file* a_file, char* a_file_name);
int process_fd_close(struct process* a_pcb, int a_fd);
struct L_fdt_elem* process_fd_get(struct process* a_pcb, int a_fd);

#pragma endregion


/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[MAX_FILE_NAME];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  struct dir* cwd;            /* Current working directory. Must be open.*/
  L_fdt fdt;          /* File descriptor table implemented as a list.*/
  L_children l_children;        /* List of child procs*/
  L_sharedData l_sharedData;   /* List of shared data*/
  struct shared_data* exit_status; /*Shared data for exit status.*/
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);


/*Parent-child relationship*/
#pragma region family
bool record_birth(struct process* a_parent, struct process* a_child, tid_t a_childPid);

struct L_children_elem* process_getChildMetadata(struct process* a_pcb, pid_t a_pid);


int process_deliver(char* a_input);
#pragma endregion

#endif /* userprog/process.h */
