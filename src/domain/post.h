#ifndef ZIOMKI_DOMAIN_POST_H
#define ZIOMKI_DOMAIN_POST_H

/* Agregat Post: wpis razem z polubieniami i komentarzami jako jedna całość. */

#include <stdbool.h>
#include <stddef.h>

#include "domain/value_objects.h"

#define MAX_LIKES 128
#define MAX_COMMENTS 16

typedef struct {
    UserId author;
    CommentText text;
    Timestamp at;
} Comment;

typedef struct {
    PostId id;
    UserId author;
    PostContent content;
    Timestamp published_at;
    UserId likes[MAX_LIKES];
    size_t like_count;
    Comment comments[MAX_COMMENTS];
    size_t comment_count;
} Post;

Post post_publish(PostId id, UserId author, PostContent content, Timestamp at);
bool post_is_liked_by(const Post *post, UserId user);
Result post_toggle_like(Post *post, UserId user, bool *liked_now);
Result post_add_comment(Post *post, UserId author, CommentText text, Timestamp at);

#endif
