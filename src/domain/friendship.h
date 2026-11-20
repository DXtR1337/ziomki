#ifndef ZIOMKI_DOMAIN_FRIENDSHIP_H
#define ZIOMKI_DOMAIN_FRIENDSHIP_H

/*
 * Usługa domenowa znajomości. Każda funkcja najpierw sprawdza wszystkie
 * reguły, a dopiero potem zmienia oba agregaty, więc błąd nigdy nie zostawia
 * relacji w połowie.
 */

#include <stdbool.h>

#include "domain/user.h"

/* Gdy druga strona już zaprosiła nadawcę, zaproszenia się spotykają
 * i od razu powstaje znajomość (*became_friends = true). */
Result friendship_invite(User *from, User *to, bool *became_friends);
Result friendship_accept(User *me, User *inviter);
Result friendship_end(User *me, User *other);

#endif
