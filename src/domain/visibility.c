#include "domain/visibility.h"

bool visibility_can_see(const User *viewer, const Post *post) {
    return user_id_equals(viewer->id, post->author) || user_is_friend(viewer, post->author);
}
