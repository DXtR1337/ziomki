#include "application/social_service.h"

#include <string.h>

#include "domain/friendship.h"
#include "domain/visibility.h"

static const UserId NO_USER = {0};
static const PostId NO_POST = {0};

static Timestamp service_now(SocialService *service) {
    return service->clock->now(service->clock->self);
}

static void emit(SocialService *service, DomainEventType type, UserId actor, UserId subject,
                 PostId post) {
    if (service->events == NULL) {
        return;
    }
    DomainEvent event;
    event.type = type;
    event.at = service_now(service);
    event.actor = actor;
    event.subject = subject;
    event.post = post;
    service->events->publish(service->events->self, &event);
}

static Result load_user(SocialService *service, UserId id, User *out) {
    return service->users->by_id(service->users->self, id, out);
}

static Result save_user(SocialService *service, const User *user) {
    return service->users->save(service->users->self, user);
}

static Result load_user_by_nick(SocialService *service, const char *nick, User *out) {
    Username name;
    TRY(username_create(nick, &name));
    return service->users->by_name(service->users->self, &name, out);
}

static Result save_post(SocialService *service, const Post *post) {
    return service->posts->save(service->posts->self, post);
}

/* Wpis, którego czytelnik nie może zobaczyć, wygląda jak wpis, którego nie ma:
 * komunikat nie zdradza, że istnieje. */
static Result load_visible_post(SocialService *service, UserId me, PostId id, Post *out) {
    User viewer;
    TRY(load_user(service, me, &viewer));
    Result found = service->posts->by_id(service->posts->self, id, out);
    if (!result_is_ok(found) || !visibility_can_see(&viewer, out)) {
        return result_fail(RESULT_NOT_FOUND,
                           "Nie ma wpisu #%u albo widzą go tylko znajomi autora.", id.value);
    }
    return result_ok();
}

Result social_register(SocialService *service, const char *nick, UserId *out_id) {
    Username name;
    TRY(username_create(nick, &name));
    User existing;
    if (result_is_ok(service->users->by_name(service->users->self, &name, &existing))) {
        return result_fail(RESULT_CONFLICT, "Nick %s jest już zajęty.", name.value);
    }
    User user = user_register(service->users->next_id(service->users->self), name,
                              service_now(service));
    TRY(save_user(service, &user));
    *out_id = user.id;
    emit(service, EVENT_USER_REGISTERED, user.id, NO_USER, NO_POST);
    return result_ok();
}

Result social_find_user(SocialService *service, const char *nick, User *out) {
    return load_user_by_nick(service, nick, out);
}

Result social_get_user(SocialService *service, UserId id, User *out) {
    return load_user(service, id, out);
}

Result social_invite(SocialService *service, UserId me, const char *nick, bool *became_friends) {
    User from;
    User to;
    TRY(load_user(service, me, &from));
    TRY(load_user_by_nick(service, nick, &to));
    TRY(friendship_invite(&from, &to, became_friends));
    TRY(save_user(service, &from));
    TRY(save_user(service, &to));
    emit(service, *became_friends ? EVENT_FRIENDSHIP_STARTED : EVENT_INVITE_SENT, from.id, to.id,
         NO_POST);
    return result_ok();
}

Result social_accept(SocialService *service, UserId me, const char *nick) {
    User self;
    User inviter;
    TRY(load_user(service, me, &self));
    TRY(load_user_by_nick(service, nick, &inviter));
    TRY(friendship_accept(&self, &inviter));
    TRY(save_user(service, &self));
    TRY(save_user(service, &inviter));
    emit(service, EVENT_FRIENDSHIP_STARTED, self.id, inviter.id, NO_POST);
    return result_ok();
}

Result social_unfriend(SocialService *service, UserId me, const char *nick) {
    User self;
    User other;
    TRY(load_user(service, me, &self));
    TRY(load_user_by_nick(service, nick, &other));
    TRY(friendship_end(&self, &other));
    TRY(save_user(service, &self));
    TRY(save_user(service, &other));
    emit(service, EVENT_FRIENDSHIP_ENDED, self.id, other.id, NO_POST);
    return result_ok();
}

Result social_publish(SocialService *service, UserId me, const char *text, PostId *out_id) {
    User author;
    TRY(load_user(service, me, &author));
    PostContent content;
    TRY(post_content_create(text, &content));
    Post post = post_publish(service->posts->next_id(service->posts->self), author.id, content,
                             service_now(service));
    TRY(save_post(service, &post));
    *out_id = post.id;
    emit(service, EVENT_POST_PUBLISHED, author.id, NO_USER, post.id);
    return result_ok();
}

Result social_toggle_like(SocialService *service, UserId me, PostId id, bool *liked_now) {
    Post post;
    TRY(load_visible_post(service, me, id, &post));
    TRY(post_toggle_like(&post, me, liked_now));
    TRY(save_post(service, &post));
    if (*liked_now) {
        emit(service, EVENT_POST_LIKED, me, post.author, post.id);
    }
    return result_ok();
}

Result social_comment(SocialService *service, UserId me, PostId id, const char *text) {
    CommentText comment;
    TRY(comment_text_create(text, &comment));
    Post post;
    TRY(load_visible_post(service, me, id, &post));
    TRY(post_add_comment(&post, me, comment, service_now(service)));
    TRY(save_post(service, &post));
    emit(service, EVENT_POST_COMMENTED, me, post.author, post.id);
    return result_ok();
}

Result social_feed(SocialService *service, UserId me, PostHeader *out, size_t max,
                   size_t *out_count) {
    User viewer;
    TRY(load_user(service, me, &viewer));
    UserId authors[MAX_FRIENDS + 1];
    authors[0] = viewer.id;
    memcpy(&authors[1], viewer.friends, viewer.friend_count * sizeof viewer.friends[0]);
    *out_count = service->posts->headers_by_authors(service->posts->self, authors,
                                                    viewer.friend_count + 1, out, max);
    return result_ok();
}

Result social_view_post(SocialService *service, UserId me, PostId id, Post *out) {
    return load_visible_post(service, me, id, out);
}

#define SUGGESTION_SCAN 512

Result social_suggestions(SocialService *service, UserId me, UserId *out, size_t max,
                          size_t *out_count) {
    User viewer;
    TRY(load_user(service, me, &viewer));
    UserId all[SUGGESTION_SCAN];
    size_t total = service->users->list_ids(service->users->self, all, SUGGESTION_SCAN);
    *out_count = 0;
    for (size_t i = total; i > 0 && *out_count < max; i--) {
        UserId candidate = all[i - 1];
        if (user_id_equals(candidate, me) || user_is_friend(&viewer, candidate) ||
            user_has_invite_from(&viewer, candidate)) {
            continue;
        }
        out[(*out_count)++] = candidate;
    }
    return result_ok();
}
