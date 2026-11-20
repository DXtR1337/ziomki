#ifndef ZIOMKI_INFRASTRUCTURE_MEMORY_STORE_H
#define ZIOMKI_INFRASTRUCTURE_MEMORY_STORE_H

/*
 * Adapter repozytoriów: dane w pamięci, zapis i odczyt całego stanu z pliku
 * binarnego. Struktura jest duża, więc trzymaj ją statycznie albo na stercie.
 */

#include <stddef.h>

#include "domain/ports.h"

#define MAX_USERS 256
#define MAX_POSTS 512

typedef struct {
    User users[MAX_USERS];
    size_t user_count;
    Post posts[MAX_POSTS];
    size_t post_count;
    unsigned next_user_id;
    unsigned next_post_id;
} MemoryStore;

void memory_store_init(MemoryStore *store);
UserRepository memory_user_repository(MemoryStore *store);
PostRepository memory_post_repository(MemoryStore *store);

/* RESULT_NOT_FOUND oznacza brak pliku, czyli start z pustą bazą. */
Result memory_store_load_file(MemoryStore *store, const char *path);
Result memory_store_save_file(const MemoryStore *store, const char *path);

#endif
