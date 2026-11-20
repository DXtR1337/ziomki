#include "domain/friendship.h"

#include "domain/id_list.h"

Result friendship_invite(User *from, User *to, bool *became_friends) {
    *became_friends = false;
    if (user_id_equals(from->id, to->id)) {
        return result_fail(RESULT_INVALID, "Nie możesz zaprosić samego siebie.");
    }
    if (user_is_friend(from, to->id)) {
        return result_fail(RESULT_CONFLICT, "Ty i %s już jesteście znajomymi.", to->name.value);
    }
    if (user_has_invited(from, to->id)) {
        return result_fail(RESULT_CONFLICT, "Zaproszenie do %s już czeka na odpowiedź.",
                           to->name.value);
    }
    if (user_has_invite_from(from, to->id)) {
        TRY(friendship_accept(from, to));
        *became_friends = true;
        return result_ok();
    }
    if (from->outgoing_count >= MAX_PENDING_INVITES) {
        return result_fail(RESULT_FULL, "Masz za dużo wysłanych zaproszeń bez odpowiedzi.");
    }
    if (to->incoming_count >= MAX_PENDING_INVITES) {
        return result_fail(RESULT_FULL, "%s ma za dużo oczekujących zaproszeń.", to->name.value);
    }
    id_list_add(from->outgoing, &from->outgoing_count, MAX_PENDING_INVITES, to->id);
    id_list_add(to->incoming, &to->incoming_count, MAX_PENDING_INVITES, from->id);
    return result_ok();
}

Result friendship_accept(User *me, User *inviter) {
    if (!user_has_invite_from(me, inviter->id)) {
        return result_fail(RESULT_NOT_FOUND, "Nie masz zaproszenia od %s.", inviter->name.value);
    }
    if (me->friend_count >= MAX_FRIENDS || inviter->friend_count >= MAX_FRIENDS) {
        return result_fail(RESULT_FULL, "Limit %d znajomych został osiągnięty.", MAX_FRIENDS);
    }
    id_list_remove(me->incoming, &me->incoming_count, inviter->id);
    id_list_remove(inviter->outgoing, &inviter->outgoing_count, me->id);
    id_list_add(me->friends, &me->friend_count, MAX_FRIENDS, inviter->id);
    id_list_add(inviter->friends, &inviter->friend_count, MAX_FRIENDS, me->id);
    return result_ok();
}

Result friendship_end(User *me, User *other) {
    if (!user_is_friend(me, other->id)) {
        return result_fail(RESULT_NOT_FOUND, "%s nie jest na liście znajomych.", other->name.value);
    }
    id_list_remove(me->friends, &me->friend_count, other->id);
    id_list_remove(other->friends, &other->friend_count, me->id);
    return result_ok();
}
