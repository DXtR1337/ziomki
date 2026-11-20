/*
 * Composition root: jedyne miejsce, które zna wszystkie warstwy naraz
 * i skleja adaptery z portami.
 */

#include <stdio.h>

#include "application/social_service.h"
#include "infrastructure/file_event_log.h"
#include "infrastructure/memory_store.h"
#include "infrastructure/system_clock.h"
#include "interface/cli.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define stdin_is_terminal() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define stdin_is_terminal() isatty(fileno(stdin))
#endif

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
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    const char *db_path = argc > 1 ? argv[1] : "ziomki.db";
    const char *log_path = argc > 2 ? argv[2] : "ziomki-zdarzenia.log";

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
    CliConfig config = {&service, stdin, stdout, !stdin_is_terminal(), persist_store, &persist};
    int code = cli_run(&config);

    if (event_log.file != NULL) {
        fclose(event_log.file);
    }
    return code;
}
