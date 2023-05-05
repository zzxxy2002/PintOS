#include "custom_lists.h"
#include <string.h>
#include "utils.h"


#pragma region L_children
void L_children_clear_func(struct list_elem* a_e) {
  struct L_children_elem* element = list_entry(a_e, struct L_children_elem, elem);
  free(element);
}

void L_children_clear(L_children* a_l) {
  list_clear(a_l, L_children_clear_func);
}

/**
 * @brief add a new argument of known size to the L_arg list
 * @return if there are any malloc errors, return false, otherwise return true
 */
bool L_arg_emplace(char* a_arg, size_t a_arg_len, struct list* a_L_arg) {
  ASSERT(a_arg != NULL);
  ASSERT(a_L_arg != NULL);
  ASSERT(a_arg_len > 0);
  struct L_arg_elem* argData = malloc(sizeof(struct L_arg_elem));
  if (argData == NULL) {
    return false;
  }
  char* arg = malloc(a_arg_len + 1);
  if (arg == NULL) {
    free(argData);
    return false;
  }
  memcpy(arg, a_arg, a_arg_len);
  arg[a_arg_len] = '\0';
  argData->arg = arg;
  argData->length = a_arg_len + 1;
  list_push_back(a_L_arg, &argData->elem);
  return true;
}
#pragma endregion

#pragma region L_arg
/**
 * @brief Parse the input string into a list of arguments and store them in an newly allocated a_L_arg struct.
 * @param a_in pointer to the unparsed input string
 * @return NULLPTR if the input string is too long/has too many arguments, or if the input string is empty, or if allocation for the list struct or its members fails.
 * @return a pointer to an L_arg struct storing all arg infos, if the parsing was successful.
 */
L_arg* L_arg_generate(char* a_in) {
  if (a_in == NULL) {
    return false;
  }
  int input_len = strlen(a_in);
  if (input_len > MAX_INPUT_STR_LEN) { /*input string can't be too long*/
    return false;
  }
  if (input_len == 0) { /*input string can't be empty*/
    return false;
  }
  struct list* L_arg = malloc(sizeof(struct list)); /*linked list storing all arguments*/
  if (L_arg == NULL) {
    return false;
  }

  list_init(L_arg);

  bool success = true;
  /*iterate through the input string, parsing arguments.*/
  int argc = 0;
  int arg_len = 0;
  char* it = a_in;
  char* arg_start = a_in;
  while (it < a_in + input_len) {
    if (*it == ' ') {
      if (arg_len > 0) {
        success = L_arg_emplace(arg_start, arg_len, L_arg);
        if (!success) {
          goto cleanup;
        }
        arg_len = 0;
        argc++;
      }
      arg_start = it + 1;
    } else {
      arg_len++;
      if (arg_len > MAX_ARG_SIZE) { /*argument can't be too long*/
        goto cleanup;
      }
    }
    it++;
  }

  /*add the last argument*/
  if (arg_len > 0) {
    success = L_arg_emplace(arg_start, arg_len, L_arg);
    if (!success) {
      goto cleanup;
    }
    argc++;
  }

  return L_arg;

cleanup: /*deallocate all memory allocated for the list all its elements in case of failure*/
  ASSERT(!success);
  list_free(L_arg, L_arg_clear_func);
  return NULL;
}

void L_arg_clear_func(struct list_elem* a_e) {
  struct L_arg_elem* a = list_entry(a_e, struct L_arg_elem, elem);
  free(a->arg);
  free(a);
}
#pragma endregion

#pragma region L_fdt
/**
 * @brief clear function for file descriptor linked list.
 * @note file_close() is called for each file descriptor instead of free().
 * @param a_e a single list element to be freed.
 */
void L_fdt_clear_func(struct list_elem* a_e) {
  struct L_fdt_elem* e = list_entry(a_e, struct L_fdt_elem, elem);
  file_close(e->file);
  free(e);
}

void L_fdt_clear(L_fdt* a_l) {
    list_clear(a_l, L_fdt_clear_func);
}

#pragma endregion

#pragma region L_activeProcs
/**
 * @brief Add a new process to the list of active processes. This function is called when a new process is created.
 */
bool L_activeProcs_add(L_activeProcs* a_l, struct process* a_pcb, char* a_name) {
    ASSERT(a_l != NULL);
    ASSERT(a_pcb != NULL);
    ASSERT(a_name != NULL);
    //ASSERT(!L_activeProcs_contains(a_l, a_pcb));
    struct L_activeProcs_elem* e = malloc(sizeof(struct L_activeProcs_elem));
    if (e == NULL) {
        return false;
    }
    e->pcb = a_pcb;
    int name_len = strlen(a_name) + 1;
    e->name = malloc(name_len);
    if (e->name == NULL) {
        free(e);
        return false;
    }
    strlcpy(e->name, a_name, name_len);
    list_push_back(a_l, &e->elem);
    return true;
}

/**
 * @brief Remove a process from the list of active processes. This function is called when a process is terminated.
 */
void L_activeProcs_remove(L_activeProcs* a_l, struct process* a_pcb) {
    ASSERT(a_l != NULL);
    ASSERT(a_pcb != NULL);
    //ASSERT(L_activeProcs_contains(a_l, a_pcb));
    struct list_elem* e = list_begin(a_l);
    while (e != list_end(a_l)) {
        struct L_activeProcs_elem* elem = list_entry(e, struct L_activeProcs_elem, elem);
        if (elem->pcb == a_pcb) {
            list_remove(e);
            free(elem->name);
            free(elem);
            return;
        }
        e = list_next(e);
    }
    NOT_REACHED();
}

bool L_activeProcs_contains(L_activeProcs* a_l, struct process* a_pcb) {
    ASSERT(a_l != NULL);
    ASSERT(a_pcb != NULL);
    struct list_elem* e = list_begin(a_l);
    while (e != list_end(a_l)) {
        struct L_activeProcs_elem* elem = list_entry(e, struct L_activeProcs_elem, elem);
        if (get_pid(elem->pcb) == get_pid(a_pcb)) {
            return true;
        }
        e = list_next(e);
    }
    return false;
}

bool L_activeProcs_contains_str(L_activeProcs* a_l, char* a_name) {
    ASSERT(a_l != NULL);
    ASSERT(a_name != NULL);
    struct list_elem* e = list_begin(a_l);
    while (e != list_end(a_l)) {
        struct L_activeProcs_elem* elem = list_entry(e, struct L_activeProcs_elem, elem);
        if (strcmp(elem->name, a_name) == 0) {
            return true;
        }
        e = list_next(e);
    }
    return false;
}