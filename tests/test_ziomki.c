#include <stdio.h>
#include <string.h>

#include "application/social_service.h"
#include "infrastructure/memory_store.h"

static int checks = 0;
static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition); \
        }                                                                    \
    } while (0)

/* --- atrapy portów --- */

static Timestamp fake_now(void *self) {
    Timestamp *tick = self;
    return ++*tick;
}

typedef struct {
    DomainEventType types[64];
    int count;
} EventRecorder;

static void record_event(void *self, const DomainEvent *event) {
    EventRecorder *recorder = self;
    if (recorder->count < 64) {
        recorder->types[recorder->count++] = event->type;
    }
}

static int count_events(const EventRecorder *recorder, DomainEventType type) {
    int n = 0;
    for (int i = 0; i < recorder->count; i++) {
        n += recorder->types[i] == type;
    }
    return n;
}

static MemoryStore store;
static MemoryStore reloaded;
static Post post;

static void test_value_objects(void) {
    Username name;
    CHECK(username_create("Kasia_1", &name).code == RESULT_OK);
    CHECK(strcmp(name.value, "kasia_1") == 0);
    CHECK(username_create("ab", &name).code == RESULT_INVALID);
    CHECK(username_create("zła", &name).code == RESULT_INVALID);
    CHECK(username_create("a234567890123456789012345", &name).code == RESULT_INVALID);

    PostContent content;
    CHECK(post_content_create("   ", &content).code == RESULT_INVALID);
    CHECK(post_content_create("  cześć  ", &content).code == RESULT_OK);
    CHECK(strcmp(content.value, "cześć") == 0);

    char long_ascii[300];
    memset(long_ascii, 'x', 281);
    long_ascii[281] = '\0';
    CHECK(post_content_create(long_ascii, &content).code == RESULT_INVALID);
    long_ascii[280] = '\0';
    CHECK(post_content_create(long_ascii, &content).code == RESULT_OK);

    char polish[POST_MAX_CHARS * 2 + 3];
    for (int i = 0; i < POST_MAX_CHARS; i++) {
        memcpy(&polish[i * 2], "ą", 2);
    }
    polish[POST_MAX_CHARS * 2] = '\0';
    CHECK(post_content_create(polish, &content).code == RESULT_OK);
    CHECK(utf8_char_count(content.value) == POST_MAX_CHARS);

    CHECK(post_content_create("\xC3(", &content).code == RESULT_INVALID);
}

static void test_social_flow(void) {
    memory_store_init(&store);
    UserRepository users = memory_user_repository(&store);
    PostRepository posts = memory_post_repository(&store);
    Timestamp tick = 1000;
    Clock clock = {&tick, fake_now};
    EventRecorder recorder = {{0}, 0};
    EventPublisher events = {&recorder, record_event};
    SocialService service = {&users, &posts, &clock, &events};

    UserId kasia, tomek, obcy;
    CHECK(social_register(&service, "kasia", &kasia).code == RESULT_OK);
    CHECK(social_register(&service, "tomek", &tomek).code == RESULT_OK);
    CHECK(social_register(&service, "obcy", &obcy).code == RESULT_OK);
    CHECK(social_register(&service, "KASIA", &kasia).code == RESULT_CONFLICT);

    bool became = true;
    CHECK(social_invite(&service, kasia, "kasia", &became).code == RESULT_INVALID);
    CHECK(social_invite(&service, kasia, "tomek", &became).code == RESULT_OK);
    CHECK(!became);
    CHECK(social_invite(&service, kasia, "tomek", &became).code == RESULT_CONFLICT);
    CHECK(social_accept(&service, kasia, "tomek").code == RESULT_NOT_FOUND);
    CHECK(social_accept(&service, tomek, "kasia").code == RESULT_OK);

    User k, t;
    CHECK(social_get_user(&service, kasia, &k).code == RESULT_OK);
    CHECK(social_get_user(&service, tomek, &t).code == RESULT_OK);
    CHECK(user_is_friend(&k, tomek) && user_is_friend(&t, kasia));
    CHECK(k.outgoing_count == 0 && t.incoming_count == 0);

    /* zaproszenia z obu stron od razu tworzą znajomość */
    CHECK(social_invite(&service, obcy, "tomek", &became).code == RESULT_OK && !became);
    CHECK(social_invite(&service, tomek, "obcy", &became).code == RESULT_OK && became);
    CHECK(social_unfriend(&service, tomek, "obcy").code == RESULT_OK);
    CHECK(social_unfriend(&service, tomek, "obcy").code == RESULT_NOT_FOUND);

    PostId p1, p2, p3;
    CHECK(social_publish(&service, kasia, "pierwszy", &p1).code == RESULT_OK);
    CHECK(social_publish(&service, tomek, "drugi", &p2).code == RESULT_OK);
    CHECK(social_publish(&service, obcy, "nikt tego nie widzi", &p3).code == RESULT_OK);
    CHECK(social_publish(&service, kasia, "", &p3).code == RESULT_INVALID);

    PostHeader feed[FEED_PAGE_SIZE];
    size_t count = 0;
    CHECK(social_feed(&service, kasia, feed, FEED_PAGE_SIZE, &count).code == RESULT_OK);
    CHECK(count == 2);
    CHECK(count == 2 && post_id_equals(feed[0].id, p2) && post_id_equals(feed[1].id, p1));

    bool liked = false;
    CHECK(social_toggle_like(&service, tomek, p1, &liked).code == RESULT_OK && liked);
    CHECK(social_toggle_like(&service, kasia, p1, &liked).code == RESULT_OK && liked);
    CHECK(social_toggle_like(&service, kasia, p1, &liked).code == RESULT_OK && !liked);
    CHECK(social_toggle_like(&service, obcy, p1, &liked).code == RESULT_NOT_FOUND);
    CHECK(social_comment(&service, obcy, p1, "hej").code == RESULT_NOT_FOUND);
    CHECK(social_comment(&service, tomek, p1, "  super  ").code == RESULT_OK);
    CHECK(social_view_post(&service, kasia, p3, &post).code == RESULT_NOT_FOUND);

    CHECK(social_view_post(&service, kasia, p1, &post).code == RESULT_OK);
    CHECK(post.like_count == 1 && user_id_equals(post.likes[0], tomek));
    CHECK(post.comment_count == 1 && strcmp(post.comments[0].text.value, "super") == 0);

    UserId suggested[8];
    size_t suggested_count = 0;
    CHECK(social_suggestions(&service, kasia, suggested, 8, &suggested_count).code == RESULT_OK);
    CHECK(suggested_count == 1 && user_id_equals(suggested[0], obcy));
    CHECK(social_suggestions(&service, obcy, suggested, 8, &suggested_count).code == RESULT_OK);
    CHECK(suggested_count == 2);

    CHECK(count_events(&recorder, EVENT_USER_REGISTERED) == 3);
    CHECK(count_events(&recorder, EVENT_FRIENDSHIP_STARTED) == 2);
    CHECK(count_events(&recorder, EVENT_FRIENDSHIP_ENDED) == 1);
    CHECK(count_events(&recorder, EVENT_POST_PUBLISHED) == 3);
    CHECK(count_events(&recorder, EVENT_POST_LIKED) == 2);
    CHECK(count_events(&recorder, EVENT_POST_COMMENTED) == 1);

    /* zapis i odczyt z pliku */
    const char *path = "tmp-test-ziomki.db";
    CHECK(memory_store_save_file(&store, path).code == RESULT_OK);
    memory_store_init(&reloaded);
    CHECK(memory_store_load_file(&reloaded, path).code == RESULT_OK);
    CHECK(reloaded.user_count == 3 && reloaded.post_count == 3);
    CHECK(reloaded.next_post_id == store.next_post_id);
    CHECK(strcmp(reloaded.posts[0].comments[0].text.value, "super") == 0);
    remove(path);
    CHECK(memory_store_load_file(&reloaded, path).code == RESULT_NOT_FOUND);
}

int main(void) {
    test_value_objects();
    test_social_flow();
    printf("%d sprawdzeń, %d błędów\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
