
#include "utils.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"


/**
 * @brief Pushes A_SIZE bytes starting from A_PTR to the stack, decrement and return ESP.
 * @param a_ptr pointer to the data to be pushed
 * @param a_size size of the data to be pushed
 * @param a_esp pointer to the stack pointer
 * @return  the new stack pointer
 */
void* push(void* a_ptr, size_t a_size, void** a_esp) {
  void* new_sp = *a_esp - a_size; ///decrement stack pointer
  *a_esp = new_sp;
  memcpy(new_sp, a_ptr, a_size); //copy the data to the stack
  return new_sp;
}

/**
 * @brief Checks if the pointer is a valid user pointer.
 * @param a_ptr pointer to be checked.
 * @return true if the pointer isn't null && is mapped && is in user virtual address space
 */
bool is_valid_user_ptr(const void* a_ptr) {
  if (a_ptr == NULL) { //duh
    return false;
  }
  if (!is_user_vaddr(a_ptr)) { //page doesn't belong to user
    return false;
  }
  if (pagedir_get_page(thread_current()->pcb->pagedir, a_ptr) == NULL) { //page isn't mapped
    return false;
  }
  return true;
}

/**
 * @brief Check if the pointer is a valid user char pointer.
 * This check implements one additional step from is_valid_user_ptr: it tries to read the char and checks
 * if the whole char is valid.
 * @param a_ptr char pointer to be checked
 */
bool is_valid_user_char_ptr(const char* a_ptr) {
  if (!is_valid_user_ptr(a_ptr)) {
    return false;
  }
  char c = *a_ptr;

  while (c != '\0') {
    if (!is_valid_user_ptr(a_ptr)) {
      return false;
    }
    c = *a_ptr;
    a_ptr++;
  }
  return true;
}

/**
 * @brief Check if the section starting from a_ptr and ending at a_ptr + a_size is a valid user vm page.
 * @param a_ptr pointer to be checked
 * @param a_size size of the section to be checked 
 */
bool is_valid_user_memory_section(const void* a_ptr, size_t a_size) {
  char* ptr = (char*) a_ptr;
  while (ptr < a_ptr + a_size) {
    if (!is_valid_user_ptr(ptr)) {
      return false;
    }
    ptr++;
  }

  return true;
}

/**
 * @brief Return the running thread's PCB.
 * @return pointer to the running thread's PCB.
 */
struct process* get_running_pcb() {
  struct process* pcb = thread_current()->pcb;
  ASSERT(pcb != NULL);
  return pcb;
}

/*Whether the Naiveos main driver proc-the proc that starts up all other procs-is running.*/
bool is_driver_process_running() {
  return thread_current()->tid == 1 && thread_current()->pcb->main_thread == 0x0;
}

/*Get the pid of the current running process.*/
pid_t get_running_pid() {
  return get_pid(get_running_pcb());
}

/*Get the pid of a pcb.*/
pid_t get_pid(struct process* a_pcb) {
  if (a_pcb->main_thread == 0x0) {
    return MAIN_PROC_ID;
  }
  return a_pcb->main_thread->tid;
}

/*Get the name of a process.*/
char* get_proc_name(struct process* a_pcb) {
  if (a_pcb->main_thread == 0x0) {
    return "main";
  }
  return a_pcb->main_thread->name;
}

void rwLock_init(rwLock* a_lock) {
  a_lock->readers = 0;
  lock_init(&a_lock->mtx_readers);
  lock_init(&a_lock->mtx_global);
}

void rLock_acquire(rwLock* a_lock) {
  lock_acquire(&a_lock->mtx_readers);
  a_lock->readers++;
  if (a_lock->readers == 1) {
    lock_acquire(&a_lock->mtx_global);
  }
  lock_release(&a_lock->mtx_readers);
}

void rLock_release(rwLock* a_lock) {
  lock_acquire(&a_lock->mtx_readers);
  a_lock->readers--;
  if (a_lock->readers == 0) {
    lock_release(&a_lock->mtx_global);
  }
  lock_release(&a_lock->mtx_readers);
}

void wLock_acquire(rwLock* a_lock) {
  lock_acquire(&a_lock->mtx_global);
}

void wLock_release(rwLock* a_lock) {
  lock_release(&a_lock->mtx_global);
}