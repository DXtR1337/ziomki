/*
 * Composition root wersji webowej: te same adaptery i ta sama warstwa
 * aplikacji co w konsoli, inny interfejs.
 */

#include <stdio.h>
#include <stdlib.h>

#include "application/social_service.h"
#include "infrastructure/file_event_log.h"
#include "infrastructure/memory_store.h"
#include "infrastructure/system_clock.h"
#include "interface/web.h"

typedef struct {
    MemoryStore *store;
    const char *path;
} PersistContext;

static void persist_store(void *context) {
    PersistContext *persist = context;
    Result result = memory_store_save_file(persist->store, persist->path);
    if (!result_is_ok(result)) {
        fprintf(stderr, "! %s\n", result.message);
    }
}

static MemoryStore store;

int main(int argc, char **argv) {
    unsigned long port = argc > 1 ? strtoul(argv[1], NULL, 10) : 8080;
    const char *db_path = argc > 2 ? argv[2] : "ziomki.db";
    const char *log_path = argc > 3 ? argv[3] : "ziomki-zdarzenia.log";
    if (port == 0 || port > 65535) {
        fprintf(stderr, "Użycie: ziomki-web [port] [plik-bazy] [plik-dziennika]\n");
        return 1;
    }

    memory_store_init(&store);
    Result loaded = memory_store_load_file(&store, db_path);
    if (!result_is_ok(loaded) && loaded.code != RESULT_NOT_FOUND) {
        fprintf(stderr, "! %s\n", loaded.message);
        return 1;
    }

    UserRepository users = memory_user_repository(&store);
    PostRepository posts = memory_post_repository(&store);
    Clock clock = system_clock();
    FileEventLog event_log = {fopen(log_path, "a"), &users};
    EventPublisher events = file_event_log_publisher(&event_log);
    SocialService service = {&users, &posts, &clock, &events};

    PersistContext persist = {&store, db_path};
    WebConfig config = {&service, (unsigned short)port, persist_store, &persist};
    return web_run(&config);
}
