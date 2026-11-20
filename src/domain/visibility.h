#ifndef ZIOMKI_DOMAIN_VISIBILITY_H
#define ZIOMKI_DOMAIN_VISIBILITY_H

#include <stdbool.h>

#include "domain/post.h"
#include "domain/user.h"

/* Polityka prywatności: wpis widzi autor i jego znajomi. */
bool visibility_can_see(const User *viewer, const Post *post);

#endif
