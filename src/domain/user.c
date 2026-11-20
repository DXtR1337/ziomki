#include "domain/user.h"

#include <string.h>

#include "domain/id_list.h"

User user_register(UserId id, Username name, Timestamp joined_at) {
    User user;
    memset(&user, 0, sizeof user);
    user.id = id;
    user.name = name;
    user.joined_at = joined_at;
    return user;
}

bool user_is_friend(const User *user, UserId other) {
    return id_list_contains(user->friends, user->friend_count, other);
}

bool user_has_invite_from(const User *user, UserId other) {
    return id_list_contains(user->incoming, user->incoming_count, other);
}

bool user_has_invited(const User *user, UserId other) {
    return id_list_contains(user->outgoing, user->outgoing_count, other);
}
