#ifndef ZIOMKI_DOMAIN_PORTS_H
#define ZIOMKI_DOMAIN_PORTS_H

/*
 * Porty: interfejsy, których domena i aplikacja potrzebują od świata.
 * W C to struktury wskaźników na funkcje z kontekstem `self`.
 * Implementacje (adaptery) leżą w warstwie infrastructure.
 */

#include <stddef.h>

#include "domain/events.h"
#include "domain/post.h"
#include "domain/user.h"

typedef struct {
    PostId id;
    UserId author;
    Timestamp published_at;
} PostHeader;

typedef struct {
    void *self;
    UserId (*next_id)(void *self);
    Result (*save)(void *self, const User *user);
    Result (*by_id)(void *self, UserId id, User *out);
    Result (*by_name)(void *self, const Username *name, User *out);
    /* Identyfikatory kont w kolejności rejestracji, najwyżej `max`. */
    size_t (*list_ids)(void *self, UserId *out, size_t max);
} UserRepository;

typedef struct {
    void *self;
    PostId (*next_id)(void *self);
    Result (*save)(void *self, const Post *post);
    Result (*by_id)(void *self, PostId id, Post *out);
    /* Nagłówki wpisów podanych autorów, od najnowszego, najwyżej `max`. */
    size_t (*headers_by_authors)(void *self, const UserId *authors, size_t author_count,
                                 PostHeader *out, size_t max);
} PostRepository;

typedef struct {
    void *self;
    Timestamp (*now)(void *self);
} Clock;

typedef struct {
    void *self;
    void (*publish)(void *self, const DomainEvent *event);
} EventPublisher;

#endif
