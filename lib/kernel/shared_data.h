#pragma once
#include <stdbool.h>

typedef int tid_t;
typedef tid_t pid_t;

/** Data structure that provides an easy and robust data-sharing interface between processes.
*/
struct shared_data;
typedef struct shared_data sharedData;

sharedData* sharedData_new(void* a_data, void (*a_func_freeData)(void*));

void sharedData_acquire(sharedData* a_sd);
void sharedData_release(sharedData* a_sd);
void sharedData_update(sharedData* a_sd, void* a_data);
void sharedData_modify(sharedData* a_sd, void (*a_func_modify)(void**));
void* sharedData_fetch(sharedData* a_sd);
bool sharedData_enter(sharedData* a_sd);
void sharedData_leave(sharedData* a_sd);
void sharedData_leaveAll();

typedef struct list L_sharedData;


