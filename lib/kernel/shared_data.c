#include "shared_data.h"
#include "utils.h"
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "userprog/process.h"
#include <debug.h>
struct L_pid_elem {
  tid_t pid;
  struct list_elem elem;
};
typedef struct list L_pid;

struct L_sharedData_elem {
    sharedData* sd;
    struct list_elem elem;
};

bool L_sharedData_insert(L_sharedData* a_lsd, sharedData* a_sd) {
  struct L_sharedData_elem* lsd_elem = malloc(sizeof(struct L_sharedData_elem));
  if (lsd_elem == NULL) {
    return false;
  }
  lsd_elem->sd = a_sd;
  list_push_back(a_lsd, &lsd_elem->elem);
  return true;
}

void L_sharedData_remove(L_sharedData* a_lsd, sharedData* a_sd) {
  struct list_elem* e;
  for (e = list_begin(a_lsd); e != list_end(a_lsd); e = list_next(e)) {
    struct L_sharedData_elem* lsd_elem = list_entry(e, struct L_sharedData_elem, elem);
    if (lsd_elem->sd == a_sd) {
      list_remove(e);
      free(lsd_elem);
      return;
    }
  }
  NOT_REACHED()
}

void L_sharedData_clearFunc(struct list_elem* a_e) {
    struct L_sharedData_elem* lsd_elem = list_entry(a_e, struct L_sharedData_elem, elem);
    shared_data_leave(lsd_elem->sd, get_running_pid());
    free(lsd_elem);
}

void L_sharedData_clear(L_sharedData* a_lsd) {
  list_clear(a_lsd, L_sharedData_clearFunc);
}

struct shared_data {
    struct semaphore sema;
    void* data;
    void (*func_freeData)(void*);
    tid_t owner_pid; /*proc id of the current owner of the shared data.*/
    struct lock mtx_ref; /*mutex for the list of processes that reference this shared data*/
    L_pid refs; /*list of proc ids that reference the shared data.*/
};

bool shared_data_referenced(sharedData* a_sd, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(a_procID != TID_ERROR);
    bool ret = false;
    lock_acquire(&a_sd->mtx_ref);
    struct list_elem* e;
    for (e = list_begin(&a_sd->refs); e != list_end(&a_sd->refs); e = list_next(e)) {
        struct L_pid_elem* elem = list_entry(e, struct L_pid_elem, elem);
        if (elem->pid == a_procID) {
            ret = true;
        }
    }
    lock_release(&a_sd->mtx_ref);
    return ret;
}

void shared_data_remove_reference(sharedData* a_sd, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(a_procID != TID_ERROR);
    lock_acquire(&a_sd->mtx_ref);
    struct list_elem* e;
    for (e = list_begin(&a_sd->refs); e != list_end(&a_sd->refs); e = list_next(e)) {
        struct L_pid_elem* elem = list_entry(e, struct L_pid_elem, elem);
        if (elem->pid == a_procID) {
            list_remove(e);
            free(elem);
            break;
        }
    }
    lock_release(&a_sd->mtx_ref);
}

bool shared_data_no_reference(sharedData* a_sd) {
    ASSERT(a_sd != NULL);
    lock_acquire(&a_sd->mtx_ref);
    bool ret = list_empty(&a_sd->refs);
    lock_release(&a_sd->mtx_ref);
    return ret;
}

/**
 * @brief Initialize a shared data struct.
 * @param a_sd pointer to the sharedData to be initialized.
 * @param a_data data to be shared.
 * @param a_pid process that calls init. The process automatically enters the shared data. However, the process has yet to acquire the data.
 * @param a_func_freeData function to free the shared data when the last reference is released. Only needed if the data is dynamically allocated.
 * @return true if the initialization is successful.
 */
bool shared_data_init(sharedData* a_sd, void* a_data, tid_t a_pid, void (*a_func_freeData)(void*)) {
    ASSERT(a_sd != NULL);
    a_sd->data = a_data;
    a_sd->func_freeData = a_func_freeData;
    sema_init(&a_sd->sema, 1); //shared data can be accessed at initialization
    lock_init(&a_sd->mtx_ref);
    list_init(&a_sd->refs);
    struct L_pid_elem* pid_e = malloc(sizeof(struct L_pid_elem));
    if (pid_e == NULL) {
        return false;
    }
    pid_e->pid = a_pid;
    list_push_back(&a_sd->refs, &pid_e->elem);
    return true;
}

/**
 * @brief Create a new shared data struct. The process calling this function automatically enters the shared data.
 * @param a_data data to be shared.
 * @param a_func_freeData function to free the shared data when the last reference is released. Only needed if the data is dynamically allocated.
 * @return pointer to the newly created sharedData, or a nullptr if memory allocation fails.
 */
sharedData* sharedData_new(void* a_data, void (*a_func_freeData)(void*)) {
    sharedData* sd = malloc(sizeof(sharedData));
    if (sd == NULL) {
        return NULL;
    }
    if (!shared_data_init(sd, a_data, get_running_pid(), a_func_freeData)) {
        free(sd);
        return NULL;
    }
    L_sharedData_insert(&get_running_pcb()->l_sharedData, sd);
    return sd;
}

/**
 * @brief Free a shared data struct and its data using free func provided by the user at its initialization.
 * @param a_sd shared data to be freed
 */
void shared_data_free(sharedData* a_sd) {
    ASSERT(a_sd != NULL);
    if (a_sd->func_freeData != NULL) {
        a_sd->func_freeData(a_sd->data);
    }
    free(a_sd);
}

void shared_data_acquire(sharedData* a_sd, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_procID));
    ASSERT(a_sd->owner_pid != a_procID); /*can't acquire a shared data that is already owned by the process*/
    sema_down(&a_sd->sema);
    a_sd->owner_pid = a_procID;
}


void shared_data_release(sharedData* a_sd, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_procID));
    ASSERT(a_sd->owner_pid == a_procID);
    sema_up(&a_sd->sema);
    a_sd->owner_pid = TID_ERROR; //shared data has no owner
}

void shared_data_update(sharedData* a_sd, void* a_data, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_procID));
    ASSERT(a_sd->owner_pid == a_procID);
    a_sd->data = a_data;
}

void shared_data_modify(sharedData* a_sd, void (*a_func_modify)(void**), tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_procID));
    ASSERT(a_sd->owner_pid == a_procID);
    a_func_modify(&a_sd->data);
}


void* shared_data_fetch(sharedData* a_sd, tid_t a_procID) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_procID));
    if (a_sd->owner_pid != a_procID) {
        sema_down(&a_sd->sema);
    }
    return a_sd->data;
}


bool shared_data_enter(sharedData* a_sd, tid_t a_pid) {
    ASSERT(a_sd != NULL);
    ASSERT(!shared_data_referenced(a_sd, a_pid));
    ASSERT(list_size(&a_sd->refs) > 0);//sanity check
    struct L_pid_elem* pid_e = malloc(sizeof(struct L_pid_elem));
    if (pid_e == NULL) {
        return false;
    }
    pid_e->pid = a_pid;
    lock_acquire(&a_sd->mtx_ref);
    list_push_back(&a_sd->refs, &pid_e->elem);
    lock_release(&a_sd->mtx_ref);
    return true;
}

void shared_data_leave(sharedData* a_sd, tid_t a_pid) {
    ASSERT(a_sd != NULL);
    ASSERT(shared_data_referenced(a_sd, a_pid));
    if (a_pid == a_sd->owner_pid) {
        shared_data_release(a_sd, a_pid);
    }
    shared_data_remove_reference(a_sd, a_pid);
    if (shared_data_no_reference(a_sd)) {
        shared_data_free(a_sd);
    }
}

/**
 * @brief Lock the shared data so that other processes have to wait for it to be released,
 * in order to fetch or edit the data.
 * In other words, become the owner of the shared data.
 * Do nothing if the process is already the owner.
 * @note Processes that call this function must call shared_data_release() before it exits or crashes.
 * @param a_sd shared data
 */
void sharedData_acquire(sharedData* a_sd) {
    shared_data_acquire(a_sd, get_running_pid());
}

/**
 * @brief Unlock the shared data so that other processes can access it.
 * The process must be the owner of the shared data.
 * @param a_sd shared data to be released.
 * @param a_procID id of the process releasing the shared data. The process must be its owner.
 */
void sharedData_release(sharedData* a_sd) {
    shared_data_release(a_sd, get_running_pid());
}

/**
 * @brief Change the shared data to a new value.
 * @param a_sd shared data
 * @param a_data new value of the shared data
 * @param a_procID editor proc of the shared data, must be the owner in order to update.
 */
void sharedData_update(sharedData* a_sd, void* a_data) {
    shared_data_update(a_sd, a_data, get_running_pid());
}

/**
 * @brief Modify shared data using a function.
 * @param a_sd shared data
 * @param a_func_modify function that takes address of shared data as argument and modifies it.
 * @param a_procID editor proc of the shared data, must be the owner in order to modify.
 */
void sharedData_modify(sharedData* a_sd, void (*a_func_modify)(void**)) {
    shared_data_modify(a_sd, a_func_modify, get_running_pid());
}
/**
 * @brief Fetch a copy of the shared data. 
 * Wait if the shared data is currently acquired or being modified by another process.
 * @param a_sd shared data from which to fetch the data.
 * @return void* copy of the shared data. Can be casted to the original type.
 */
void* sharedData_fetch(sharedData* a_sd) {
    return shared_data_fetch(a_sd, get_running_pid());
}

/**
 * @brief Become one of the references to the shared data.
 * @note Processes that call this function must call shared_data_leave() before it exits or crashes.
 * @param a_sd shared data
 * @return true if the process successfully becomes a reference. Note the return value can only be false on memory allocation failure.
 */
bool sharedData_enter(sharedData* a_sd) {
    struct process* pcb = get_running_pcb();
    L_sharedData_insert(&pcb->l_sharedData, a_sd);
    return shared_data_enter(a_sd, get_running_pid());
}

/**
 * @brief Leave the shared data reference. 
 * If the process is the owner of the shared data, it will be released.
 * If the shared data is no longer referenced by any process, free it.
 * @param a_sd shared data
 */
void sharedData_leave(sharedData* a_sd) {
    struct process* pcb = get_running_pcb();
    L_sharedData_remove(&pcb->l_sharedData, a_sd);
    shared_data_leave(a_sd, get_running_pid());
}

/**
 * @brief Leave all shared data references of the calling process.
 * In additon, clear the shared data list of the calling process.
 */
void sharedData_leaveAll() {
    struct process* proc = get_running_pcb();
    L_sharedData_clear(&proc->l_sharedData);
}
