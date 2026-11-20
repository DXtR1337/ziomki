#include "domain/id_list.h"

#include <string.h>

bool id_list_contains(const UserId *list, size_t count, UserId id) {
    for (size_t i = 0; i < count; i++) {
        if (user_id_equals(list[i], id)) {
            return true;
        }
    }
    return false;
}

bool id_list_add(UserId *list, size_t *count, size_t capacity, UserId id) {
    if (id_list_contains(list, *count, id) || *count >= capacity) {
        return false;
    }
    list[(*count)++] = id;
    return true;
}

bool id_list_remove(UserId *list, size_t *count, UserId id) {
    for (size_t i = 0; i < *count; i++) {
        if (user_id_equals(list[i], id)) {
            memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof *list);
            (*count)--;
            return true;
        }
    }
    return false;
}
