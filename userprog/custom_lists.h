#pragma once
#include <list.h>
#include <shared_data.h>
#include "threads/thread.h"

#define MAX_FILE_NAME 16 /*Maximum length of a proc's file name to be recorded.*/
#define MAX_INPUT_STR_LEN 1024 /*force the maximum input string length to an arbitrary value, currently being 1024*/
#define MAX_ARG_SIZE 256             /*force maximum argument size to 256 for now*/


/*a congregation of custom Naiveos lists*/
typedef struct list L_children;
struct L_children_elem { /*list element for child_l*/
  tid_t pid;
  struct process* pcb; /**Child's pcb. Might be nullptr if child exits before parent.*/
  bool have_waited;
  struct sharedData* exit_status;
  struct list_elem elem;
};
void L_children_clear_func(struct list_elem* a_e);
void L_children_clear(L_children* a_l);


/**List element of L_arg, Naiveos list storing all arguments for a function.*/
typedef struct list L_arg;
struct L_arg_elem {
  char* arg;  /*char pointer to the argument*/
  int length; /*length of the argument, including the null terminator*/
  struct list_elem elem;
};
L_arg* L_arg_generate(char* a_in);
void L_arg_clear_func(struct list_elem* a_e);


/**
 * @brief List element a file descriptor table as a list.
 * To look up a file descriptor, iterate through the list and find descriptor with right id.
 */
typedef struct list L_fdt;
struct L_fdt_elem {
  struct file* file;
  int id;
  char file_name[MAX_FILE_NAME];
  struct list_elem elem;
};
void L_fdt_clear_func(struct list_elem* a_e);
void L_fdt_clear(L_fdt* a_l);
/**
 * @brief A list of active processes running by the OS.
 */
typedef struct list L_activeProcs;
struct L_activeProcs_elem {
  struct process* pcb;
  char* name;
  struct list_elem elem;
};
bool L_activeProcs_add(L_activeProcs* a_l, struct process* a_pcb, char* a_name);
void L_activeProcs_remove(L_activeProcs* a_l, struct process* a_pcb);
bool L_activeProcs_contains(L_activeProcs* a_l, struct process* a_pcb);
bool L_activeProcs_contains_str(L_activeProcs* a_l, char* a_name);