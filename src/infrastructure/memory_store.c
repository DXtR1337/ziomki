#include "infrastructure/memory_store.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "domain/id_list.h"

#define STORE_MAGIC "ZIOMKI02"

typedef struct {
    char magic[8];
    uint32_t user_size;
    uint32_t post_size;
    uint32_t user_count;
    uint32_t post_count;
    uint32_t next_user_id;
    uint32_t next_post_id;
} StoreHeader;

void memory_store_init(MemoryStore *store) {
    memset(store, 0, sizeof *store);
    store->next_user_id = 1;
    store->next_post_id = 1;
}

/* --- UserRepository --- */

static UserId user_next_id(void *self) {
    MemoryStore *store = self;
    UserId id = {store->next_user_id++};
    return id;
}

static Result user_save(void *self, const User *user) {
    MemoryStore *store = self;
    for (size_t i = 0; i < store->user_count; i++) {
        if (user_id_equals(store->users[i].id, user->id)) {
            store->users[i] = *user;
            return result_ok();
        }
    }
    if (store->user_count >= MAX_USERS) {
        return result_fail(RESULT_FULL, "Baza ma już komplet %d kont.", MAX_USERS);
    }
    store->users[store->user_count++] = *user;
    return result_ok();
}

static Result user_by_id(void *self, UserId id, User *out) {
    MemoryStore *store = self;
    for (size_t i = 0; i < store->user_count; i++) {
        if (user_id_equals(store->users[i].id, id)) {
            *out = store->users[i];
            return result_ok();
        }
    }
    return result_fail(RESULT_NOT_FOUND, "Nie ma użytkownika #%u.", id.value);
}

static Result user_by_name(void *self, const Username *name, User *out) {
    MemoryStore *store = self;
    for (size_t i = 0; i < store->user_count; i++) {
        if (strcmp(store->users[i].name.value, name->value) == 0) {
            *out = store->users[i];
            return result_ok();
        }
    }
    return result_fail(RESULT_NOT_FOUND, "Nie ma użytkownika %s.", name->value);
}

static size_t user_list_ids(void *self, UserId *out, size_t max) {
    MemoryStore *store = self;
    size_t count = 0;
    for (size_t i = 0; i < store->user_count && count < max; i++) {
        out[count++] = store->users[i].id;
    }
    return count;
}

UserRepository memory_user_repository(MemoryStore *store) {
    UserRepository repository = {store,      user_next_id, user_save,
                                 user_by_id, user_by_name, user_list_ids};
    return repository;
}

/* --- PostRepository --- */

static PostId post_next_id(void *self) {
    MemoryStore *store = self;
    PostId id = {store->next_post_id++};
    return id;
}

static Result post_save(void *self, const Post *post) {
    MemoryStore *store = self;
    for (size_t i = 0; i < store->post_count; i++) {
        if (post_id_equals(store->posts[i].id, post->id)) {
            store->posts[i] = *post;
            return result_ok();
        }
    }
    if (store->post_count >= MAX_POSTS) {
        return result_fail(RESULT_FULL, "Baza ma już komplet %d wpisów.", MAX_POSTS);
    }
    store->posts[store->post_count++] = *post;
    return result_ok();
}

static Result post_by_id(void *self, PostId id, Post *out) {
    MemoryStore *store = self;
    for (size_t i = 0; i < store->post_count; i++) {
        if (post_id_equals(store->posts[i].id, id)) {
            *out = store->posts[i];
            return result_ok();
        }
    }
    return result_fail(RESULT_NOT_FOUND, "Nie ma wpisu #%u.", id.value);
}

/* Wpisy leżą w kolejności publikacji, więc przegląd od końca daje najnowsze. */
static size_t post_headers_by_authors(void *self, const UserId *authors, size_t author_count,
                                      PostHeader *out, size_t max) {
    MemoryStore *store = self;
    size_t count = 0;
    for (size_t i = store->post_count; i > 0 && count < max; i--) {
        const Post *post = &store->posts[i - 1];
        if (id_list_contains(authors, author_count, post->author)) {
            out[count].id = post->id;
            out[count].author = post->author;
            out[count].published_at = post->published_at;
            count++;
        }
    }
    return count;
}

PostRepository memory_post_repository(MemoryStore *store) {
    PostRepository repository = {store, post_next_id, post_save, post_by_id,
                                 post_headers_by_authors};
    return repository;
}

/* --- Plik --- */

Result memory_store_save_file(const MemoryStore *store, const char *path) {
    char temp_path[1024];
    int written = snprintf(temp_path, sizeof temp_path, "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof temp_path) {
        return result_fail(RESULT_IO, "Ścieżka pliku jest za długa.");
    }
    FILE *file = fopen(temp_path, "wb");
    if (file == NULL) {
        return result_fail(RESULT_IO, "Nie mogę otworzyć %s do zapisu.", temp_path);
    }
    StoreHeader header;
    memset(&header, 0, sizeof header);
    memcpy(header.magic, STORE_MAGIC, sizeof header.magic);
    header.user_size = (uint32_t)sizeof(User);
    header.post_size = (uint32_t)sizeof(Post);
    header.user_count = (uint32_t)store->user_count;
    header.post_count = (uint32_t)store->post_count;
    header.next_user_id = store->next_user_id;
    header.next_post_id = store->next_post_id;

    int ok = fwrite(&header, sizeof header, 1, file) == 1 &&
             fwrite(store->users, sizeof(User), store->user_count, file) == store->user_count &&
             fwrite(store->posts, sizeof(Post), store->post_count, file) == store->post_count;
    if (fclose(file) != 0) {
        ok = 0;
    }
    if (!ok) {
        remove(temp_path);
        return result_fail(RESULT_IO, "Zapis do %s się nie udał.", temp_path);
    }
    remove(path);
    if (rename(temp_path, path) != 0) {
        return result_fail(RESULT_IO, "Nie mogę podmienić pliku %s.", path);
    }
    return result_ok();
}

Result memory_store_load_file(MemoryStore *store, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return result_fail(RESULT_NOT_FOUND, "Brak pliku %s.", path);
    }
    Result result = result_ok();
    StoreHeader header;
    if (fread(&header, sizeof header, 1, file) != 1 ||
        memcmp(header.magic, STORE_MAGIC, sizeof header.magic) != 0 ||
        header.user_size != sizeof(User) || header.post_size != sizeof(Post) ||
        header.user_count > MAX_USERS || header.post_count > MAX_POSTS) {
        result = result_fail(RESULT_IO, "Plik %s ma nieznany format.", path);
    } else {
        memory_store_init(store);
        if (fread(store->users, sizeof(User), header.user_count, file) != header.user_count ||
            fread(store->posts, sizeof(Post), header.post_count, file) != header.post_count) {
            memory_store_init(store);
            result = result_fail(RESULT_IO, "Plik %s jest ucięty.", path);
        } else {
            store->user_count = header.user_count;
            store->post_count = header.post_count;
            store->next_user_id = header.next_user_id;
            store->next_post_id = header.next_post_id;
        }
    }
    fclose(file);
    return result;
}
