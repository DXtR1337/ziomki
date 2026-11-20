#ifndef ZIOMKI_APPLICATION_SOCIAL_SERVICE_H
#define ZIOMKI_APPLICATION_SOCIAL_SERVICE_H

/*
 * Warstwa aplikacji: przypadki użycia. Każda funkcja wczytuje agregaty przez
 * porty, deleguje reguły do domeny, zapisuje wynik i publikuje zdarzenie.
 * Nie zna plików, konsoli ani formatu zapisu.
 */

#include <stdbool.h>
#include <stddef.h>

#include "domain/ports.h"

#define FEED_PAGE_SIZE 20

typedef struct {
    UserRepository *users;
    PostRepository *posts;
    Clock *clock;
    EventPublisher *events; /* może być NULL */
} SocialService;

Result social_register(SocialService *service, const char *nick, UserId *out_id);
Result social_find_user(SocialService *service, const char *nick, User *out);
Result social_get_user(SocialService *service, UserId id, User *out);

Result social_invite(SocialService *service, UserId me, const char *nick, bool *became_friends);
Result social_accept(SocialService *service, UserId me, const char *nick);
Result social_unfriend(SocialService *service, UserId me, const char *nick);

Result social_publish(SocialService *service, UserId me, const char *text, PostId *out_id);
Result social_toggle_like(SocialService *service, UserId me, PostId post, bool *liked_now);
Result social_comment(SocialService *service, UserId me, PostId post, const char *text);

Result social_feed(SocialService *service, UserId me, PostHeader *out, size_t max,
                   size_t *out_count);
Result social_view_post(SocialService *service, UserId me, PostId post, Post *out);

/* Osoby spoza znajomych, od najnowszych kont. Pomija tych, którzy już
 * zaprosili `me` (ich widać w zaproszeniach). */
Result social_suggestions(SocialService *service, UserId me, UserId *out, size_t max,
                          size_t *out_count);

#endif
