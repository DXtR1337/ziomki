#ifndef ZIOMKI_DOMAIN_EVENTS_H
#define ZIOMKI_DOMAIN_EVENTS_H

#include "domain/value_objects.h"

typedef enum {
    EVENT_USER_REGISTERED,
    EVENT_INVITE_SENT,
    EVENT_FRIENDSHIP_STARTED,
    EVENT_FRIENDSHIP_ENDED,
    EVENT_POST_PUBLISHED,
    EVENT_POST_LIKED,
    EVENT_POST_COMMENTED
} DomainEventType;

typedef struct {
    DomainEventType type;
    Timestamp at;
    UserId actor;
    UserId subject;
    PostId post;
} DomainEvent;

#endif
