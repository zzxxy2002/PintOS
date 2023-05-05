#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/buffer_cache.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <list.h>
#include "lib/utils.h"
#include "custom_lists.h"

static int exit_code; /*most recent exit code*/

static thread_func start_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
bool setup_thread(void (**eip)(void), void** esp);


#define USERPROG_STACK_ALIGN_BYTE 16 /*stack alignment value for user programs*/


#pragma region file
/**
 * @brief Check if the process with name is currently active.
 * @param a_name 
 */
bool process_is_active_name(char* a_name) {
  return L_activeProcs_contains_str(&active_procs, a_name);
}
/**
 * @brief Close all opened file descriptors of a process.
 * @param a_pcb pcb of the process whose file descriptors are to be closed.
 */
void process_clear_L_fdt(struct process* a_pcb) {
  L_fdt_clear(&a_pcb->fdt);
}

/*helper function for process_fd_open() that generates a none-conflicting FD id.*/
int generate_fd_id(struct process* a_pcb) {
  int id = 3; /*0 and 1 are reserved for stdin and stdout*/
  struct list_elem* e;
  for (e = list_begin(&a_pcb->fdt); e != list_end(&a_pcb->fdt); e = list_next(e)) {
    struct L_fdt_elem* fdt_e = list_entry(e, struct L_fdt_elem, elem);
    if (fdt_e->id == id) {
      id++;
    }
  }
  return id; 
}

/**
 * @brief add a file descriptor to a process' FDT.
 * @param a_pcb process opening the file
 * @param a_file name of the file opened
 * @return ID of the added file descriptor.
 */
int process_fd_open(struct process* a_pcb, struct file* a_file, char* a_file_name) {
  ASSERT(a_file != NULL);
  struct L_fdt_elem* e = malloc(sizeof(struct L_fdt_elem));
  e->file = a_file;
  e->id = generate_fd_id(a_pcb);
  strlcpy(e->file_name, a_file_name, MIN(strlen(a_file_name) + 1, MAX_FILE_NAME));
  list_push_back(&a_pcb->fdt, &e->elem);
  return e->id;
}

/**
 * @brief Close a file descriptor from a process' FDT.
 * @param a_pcb process closing the file
 * @param a_fdt_id name of the file that will be closed
 * @return file descriptor of the given ID, or -1 if no corresponding file descriptor is found.
 */
int process_fd_close(struct process* a_pcb, int a_fd) {
  struct list_elem* e;
  for (e = list_begin(&a_pcb->fdt); e != list_end(&a_pcb->fdt); e = list_next(e)) {
    struct L_fdt_elem* fdt_e = list_entry(e, struct L_fdt_elem, elem);
    if (fdt_e->id == a_fd) {
      file_close(fdt_e->file);
      list_remove(e);
      free(fdt_e);
      return 0;
    }
  }
  return -1;
}

/**
 * @brief Get the file struct corresponding to a file descriptor ID.
 * @param a_pcb 
 * @param a_fd 
 * @return file descriptor of the given ID, or NULL if no corresponding file descriptor is found.
 */
struct L_fdt_elem* process_fd_get(struct process* a_pcb, int a_fd) {
  struct list_elem* e;
  for (e = list_begin(&a_pcb->fdt); e != list_end(&a_pcb->fdt); e = list_next(e)) {
    struct L_fdt_elem* fdt_e = list_entry(e, struct L_fdt_elem, elem);
    if (fdt_e->id == a_fd) {
      return fdt_e;
    }
  }
  return NULL;
}

#pragma endregion

#pragma region arg_passing
/**
 * @brief Prealigns the stack pointer to a certain value before pushing function call arguments,
 * so that the stack pointer, when the function is called, is aligned to a 16-byte boundary.
 * @param a_arg_bytes total bytes of arguments to be pushed after prealignment.
 * @param a_esp pointer to the stack pointer.
 * @return true always for now.
 */
bool pre_align_stack(void** a_esp, size_t a_arg_bytes) {
  void* esp = *a_esp;
  void* unpadded_esp = esp - a_arg_bytes;
  size_t padding = ((size_t)unpadded_esp % 16);
  *a_esp = esp - padding;
  return true;
}

/**
 * @brief Push argc and argv onto the program stack and align the stack to 16 byte per calling convention.
 * https://cs162.org/static/proj/Naiveos-docs/docs/userprog/program-startup/
 * @return true if no error occurs. false otherwise.
 */
bool push_args(int a_argc, struct list* a_L_arg, void** a_esp) {
  /*Iterate through a_L_arg and push each argument onto the stack*/

  char** argAddr = malloc(sizeof(char*) * a_argc); //array of ptrs to the arguments
  if (argAddr == NULL) {
    return false;
  }

  bool success = true;

  int i = 0;
  struct list_elem* e;
  for (e = list_rbegin(a_L_arg); e != list_rend(a_L_arg); e = list_prev(e)) {
    struct L_arg_elem* argData = list_entry(e, struct L_arg_elem, elem);
    size_t size = (argData->length) * sizeof(char);
    void* new_sp = push(argData->arg, argData->length, a_esp);
    argAddr[i] = new_sp; //store the ptr to the argument
    i++;
  }
  size_t arg_space = (a_argc + 1) * sizeof(char*) + //ptr to allarguments + null sentinel
                     (2 * sizeof(void*));           //argc + argv

  success = pre_align_stack(a_esp, arg_space);
  if (!success) {
    goto done;
  }

  /*Push the null pointer sentinel*/
  char* sentinel = NULL;
  push(&sentinel, sizeof(char*), a_esp);
  /*Push addresses of all args onto the stack*/
  for (i = 0; i < a_argc; i++) {
    push(&argAddr[i], sizeof(char*), a_esp);
  }

  /*Push char* argv[]*/
  char** argv = *a_esp;
  push(&argv, sizeof(char**), a_esp);
  /*Push int argc*/
  push(&a_argc, sizeof(int), a_esp);

done:
  free(argAddr);
  return success;
}

/**
 * @brief Pushes a fake return address onto the stack
 * @param a_esp 
 * @return true always, for now. 
 */
bool push_retAddr(void** a_esp) {
  void* retAddr = (void*)0;
  push(&retAddr, sizeof(void*), a_esp);
  return true;
}

/**
 * @brief Initialize a user program's stack by doing the following:
 *  1. push all arguments onto the stack.
 *  2. align stack per calling convention's specs.(needs to be done before the fake ret address is pushed)
 *  3. push a fake return address onto the stack.
 * @param a_L_arg list of arguments to be pushed onto the stack
 * @param a_esp pointer to the stack pointer.
 * @return true if no error occurs. false otherwise.
 */
bool initialize_stack(struct list* a_L_arg, void** a_esp) {
  bool success = true;
  if (success) {
    success = push_args(list_size(a_L_arg), a_L_arg, a_esp);
  }
  if (success) {
    success = push_retAddr(a_esp);
  }

  return success;
}

#pragma endregion

#pragma region family
static bool load_success; /*indicate whether the most recent program load is successful*/
static struct process* loadedProc; /*the process that is loaded by the most recent program load*/
//static struct semaphore prog_load; /*semaphore for the parent to wait for the child to load*/
/**
 * @brief Get a child process's pcb struct from its pid.
 * @param a_pcb Process's pcb.
 * @param a_pid Child's pid.
 * @return Child's pcb. NULL if not found.
 */
struct L_children_elem* process_getChildMetadata(struct process* a_pcb, pid_t a_pid) {
  struct list_elem* e;
  for (e = list_begin(&a_pcb->l_children); e != list_end(&a_pcb->l_children); e = list_next(e)) {
    struct L_children_elem* child = list_entry(e, struct L_children_elem, elem);
    if (child->pid == a_pid) {
      return child;
    }
  }
  return NULL;
}

void process_clear_L_children(struct process* a_pcb) {
  struct list* child_l = &a_pcb->l_children;
  L_children_clear(child_l);
}

bool record_birth(struct process* a_parent, struct process* a_child, tid_t a_childPid) {
  struct L_children_elem* childData = malloc(sizeof(struct L_children_elem));
  if (childData == NULL) {
    return false;
  }
  childData->pid = a_childPid;
  childData->exit_status = a_child->exit_status;
  childData->have_waited = false;
  if (!sharedData_enter(childData->exit_status)) {/*Parent grabs shared data*/
    return false;
  }
  list_push_back(&a_parent->l_children, &childData->elem);
  return true;
}


/**
* @brief Waits for process with PID child_pid to die and returns its exit status.
* 
* @param a_child_pid process ID of the child process to wait for.
* @return child's exit status.
* @return -1 if the child process is not found or if the process is already waiting for the child.
*/
int process_wait(pid_t a_child_pid) {
  struct L_children_elem* childMeta = process_getChildMetadata(get_running_pcb(), a_child_pid);

  if (childMeta == NULL) {
    return -1;
  }
  if (childMeta->have_waited) {
    return -1;
  }
  childMeta->have_waited = true;

  sharedData* childExitStatus = childMeta->exit_status;
  pid_t parent_pid = get_running_pid();

  int exit = sharedData_fetch(childExitStatus); 

  sharedData_leave(childExitStatus);

  return exit;
}


#pragma endregion

#pragma region process initialization
/* Initializes user programs in the system by ensuring the main
   thread has a minimal PCB so that it can execute and wait for
   the first user process. Any additions to the PCB should be also
   initialized here if main needs those members */
void userprog_init(void) {
  struct thread* t = thread_current();
  bool success;

  /* Allocate process control block
     It is imoprtant that this is a call to calloc and not malloc,
     so that t->pcb->pagedir is guaranteed to be NULL (the kernel's
     page directory) when t->pcb is assigned, because a timer interrupt
     can come at any time and activate our pagedir */
  t->pcb = calloc(sizeof(struct process), 1);
  success = t->pcb != NULL;

  /* Kill the kernel if we did not succeed */
  ASSERT(success);
}

typedef struct start_process_args {
  char* file_name; /*file name of the program to be executed*/
  struct list* arg_l; /*L_arg list of parsed arguments*/
  struct semaphore sema_child; /*semaphore for the parent to wait for the child to load*/
  struct semaphore sema_parent; /*semaphore for the child to wait for the parent.*/
  bool success; /*address to store the result of the program load*/
  struct process* loadedProc; /*Address to store the loaded process*/
} umbilical;

/* Starts a new thread based on the user input a_input. 
  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* a_input) {
  tid_t child_pid;
  /*=============Memory Allocation================*/
  L_arg* parsed_args = L_arg_generate(a_input);
  if (parsed_args == NULL) {
    return TID_ERROR;
  }

  umbilical* umbilic = malloc(sizeof(umbilical));
  if (umbilic == NULL) {
    free(parsed_args);
    return TID_ERROR;
  }
  /*=============Memory Allocation================*/

  char* fileName = list_entry(list_front(parsed_args), struct L_arg_elem, elem)->arg;

  sema_init(&umbilic->sema_child, 0);
  sema_init(&umbilic->sema_parent, 0);
  umbilic->arg_l = parsed_args;
  umbilic->file_name = fileName;

  child_pid = thread_create(fileName, PRI_DEFAULT, start_process, umbilic);

  if (child_pid == TID_ERROR) {
    goto cleanup;
  }

  sema_down(&umbilic->sema_child); /* wait for the child program to load*/

  /*==================Child program finished loading=====================*/
  if (!umbilic->success) {
    child_pid = TID_ERROR;
    goto cleanup;
  }

  struct process* child = umbilic->loadedProc;
  ASSERT(child != NULL);
  struct process* curr = get_running_pcb();
  ASSERT(curr != NULL);

  bool res;
  res = record_birth(curr, child, child_pid);
  if (!res) {
     child_pid = TID_ERROR;
     goto cleanup;
  }

  //CWD inheritance
  if (curr->cwd != NULL) {
    child->cwd = dir_reopen(curr->cwd);
  } else { // parent has no cwd(driver process)
    ASSERT(is_driver_process_running());
    child->cwd = dir_open_root();
  }
  ASSERT(child->cwd != NULL);

  sema_up(&umbilic->sema_parent); /* signal the child that the parent is done recording the birth*/
  sema_down(&umbilic->sema_child); /* wait for the child to get out of sema_parent lock to deallocate umbilic*/


cleanup:
  free(umbilic);
  list_free(parsed_args, L_arg_clear_func);
  return child_pid;
}


/**
 * @brief a thread function that loads a user program and starts it running.
 * @param a_umbilic tie connecting a parent and a child proc; destroyed when the child is done loading.
 */
static void start_process(void* a_umbilic) {
  umbilical* umbilic = (umbilical*)a_umbilic;
  struct list* L_arg = umbilic->arg_l;

  char* file_name = umbilic->file_name;

  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;
    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->pcb->process_name, t->name, sizeof t->name);
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    if_.saved_fpu_state = t->saved_fpu_state; /*save FPU state onto i-frame to be restored by the interrupt handler*/
#if !ENABLE_BUFFER_CACHE
    lock_acquire(&hackyLock); /*Acquire file lock to protect the binary from being written into. */
#endif
    success = load(file_name, &if_.eip, &if_.esp);
#if !ENABLE_BUFFER_CACHE
    lock_release(&hackyLock); /*Release file lock*/
#endif
  }
  /*initialize fd_list*/
  if (success) {
    list_init(&t->pcb->fdt); /*initialize file descriptor table*/
    list_init(&t->pcb->l_children); /*initialize child process list*/
    list_init(&t->pcb->l_sharedData); /*initialize file list*/
  }

  if (success) {
    /* Initialize stack */
    success = initialize_stack(L_arg, &if_.esp);
  }

  if (success) {
   success = L_activeProcs_add(&active_procs, t->pcb, file_name);
  }

  /* Handle failure with succesful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortuantely timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    t->pcb = NULL;
    free(pcb_to_free);
  }

    /*init shared data*/
  if (success) {
    sharedData* exit_status = sharedData_new(-1, NULL);
    if (exit_status) {
      t->pcb->exit_status = exit_status;
      sharedData_acquire(exit_status);
    } else {
      success = false;
    }
  }

  umbilic->success = success;
  umbilic->loadedProc = success ? t->pcb : NULL;
  sema_up(&umbilic->sema_child);  /*signal that the program has done loading and has saved its loading success state.*/

  /*Exit on failure or jump to userspace */
  if (!success) {
    thread_exit();
  }

  sema_down(&umbilic->sema_parent); /*wait for parent to finish recording a successful birth.*/
  sema_up(&umbilic->sema_child); /*signal the parent the child's free. Parent can deallocate umbilic struct*/

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

/**
 * @brief Free the current process's resources.
 * @note Naiveos driver process won't call this function to exit.
 */
void process_exit(void) {
  ASSERT(!is_driver_process_running());
  struct thread* cur = thread_current();
    uint32_t* pd;
  struct process* pcb = cur->pcb;
  /* If this thread does not have a PCB, don't worry */
  if (pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  }
  /* Close all processes' file descriptors */
  process_clear_L_fdt(pcb);
  /* free process's child list*/
  process_clear_L_children(pcb);
  /* proc no longer active*/
  L_activeProcs_remove(&active_procs, pcb);

  sharedData_leave(pcb->exit_status);

  sharedData_leaveAll();

  //if (pcb->cwd) dir_close(pcb->cwd); /*Close CWD*/

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pcb->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
         cur->pcb->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  /* Free the PCB of this process and kill this thread
     Avoid race where PCB is freed before t->pcb is set to NULL
     If this happens, then an unfortuantely timed timer interrupt
     can try to activate the pagedir, but it is now freed memory */
  struct process* pcb_to_free = cur->pcb;
  cur->pcb = NULL;
  free(pcb_to_free);

  thread_exit();
}

/* Sets up the CPU for running user code in the current
   thread. This function is called on every context switch. */
void process_activate(void) {
  struct thread* t = thread_current();

  /* Activate thread's page tables. */
  if (t->pcb != NULL && t->pcb->pagedir != NULL)
    pagedir_activate(t->pcb->pagedir);
  else
    pagedir_activate(NULL);

  /* Set thread's kernel stack for use in processing interrupts.
     This does nothing if this is not a user process. */
  tss_update();
}
#pragma endregion

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp) {
  struct thread* t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file* file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  file = filesys_open(file_name);
  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 ||
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
                     Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
          } else {
            /* Entirely zero.
                     Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  file_close(file);
  return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void*)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void*)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t* kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable)) {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack(void** esp) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

/* Returns true if t is the main thread of the process p */
bool is_main_thread(struct thread* t, struct process* p) { return p->main_thread == t; }


/* Creates a new stack for the thread and sets up its arguments.
   Stores the thread's entry point into *EIP and its initial stack
   pointer into *ESP. Handles all cleanup if unsuccessful. Returns
   true if successful, false otherwise.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. You may find it necessary to change the
   function signature. */
bool setup_thread(void (**eip)(void) UNUSED, void** esp UNUSED) { return false; }

/* Starts a new thread with a new user stack running SF, which takes
   TF and ARG as arguments on its user stack. This new thread may be
   scheduled (and may even exit) before pthread_execute () returns.
   Returns the new thread's TID or TID_ERROR if the thread cannot
   be created properly.

   This function will be implemented in Project 2: Multithreading and
   should be similar to process_execute (). For now, it does nothing.
   */
tid_t pthread_execute(stub_fun sf UNUSED, pthread_fun tf UNUSED, void* arg UNUSED) { return -1; }

/* A thread function that creates a new user thread and starts it
   running. Responsible for adding itself to the list of threads in
   the PCB.

   This function will be implemented in Project 2: Multithreading and
   should be similar to start_process (). For now, it does nothing. */
static void start_pthread(void* exec_ UNUSED) {}

/* Waits for thread with TID to die, if that thread was spawned
   in the same process and has not been waited on yet. Returns TID on
   success and returns TID_ERROR on failure immediately, without
   waiting.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
tid_t pthread_join(tid_t tid UNUSED) { return -1; }

/* Free the current thread's resources. Most resources will
   be freed on thread_exit(), so all we have to do is deallocate the
   thread's userspace stack. Wake any waiters on this thread.

   The main thread should not use this function. See
   pthread_exit_main() below.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit(void) {}

/* Only to be used when the main thread explicitly calls pthread_exit.
   The main thread should wait on all threads in the process to
   terminate properly, before exiting itself. When it exits itself, it
   must terminate the process in addition to all necessary duties in
   pthread_exit.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit_main(void) {}
