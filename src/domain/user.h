#ifndef ZIOMKI_DOMAIN_USER_H
#define ZIOMKI_DOMAIN_USER_H

/*
 * Agregat User: konto razem z listą znajomych i zaproszeń. Relacja znajomości
 * dotyczy dwóch agregatów naraz, więc jej reguły trzyma usługa domenowa
 * w friendship.h, a nie sam User.
 */

#include <stdbool.h>
#include <stddef.h>

#include "domain/value_objects.h"

#define MAX_FRIENDS 64
#define MAX_PENDING_INVITES 32

typedef struct {
    UserId id;
    Username name;
    Timestamp joined_at;
    UserId friends[MAX_FRIENDS];
    size_t friend_count;
    UserId incoming[MAX_PENDING_INVITES];
    size_t incoming_count;
    UserId outgoing[MAX_PENDING_INVITES];
    size_t outgoing_count;
} User;

User user_register(UserId id, Username name, Timestamp joined_at);
bool user_is_friend(const User *user, UserId other);
bool user_has_invite_from(const User *user, UserId other);
bool user_has_invited(const User *user, UserId other);

#endif
