#ifndef ZIOMKI_DOMAIN_ID_LIST_H
#define ZIOMKI_DOMAIN_ID_LIST_H

#include <stdbool.h>
#include <stddef.h>

#include "domain/value_objects.h"

/* Mały zbiór identyfikatorów na tablicy o stałej pojemności. */
bool id_list_contains(const UserId *list, size_t count, UserId id);
bool id_list_add(UserId *list, size_t *count, size_t capacity, UserId id);
bool id_list_remove(UserId *list, size_t *count, UserId id);

#endif
