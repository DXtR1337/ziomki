#include "domain/post.h"

#include <string.h>

#include "domain/id_list.h"

Post post_publish(PostId id, UserId author, PostContent content, Timestamp at) {
    Post post;
    memset(&post, 0, sizeof post);
    post.id = id;
    post.author = author;
    post.content = content;
    post.published_at = at;
    return post;
}

bool post_is_liked_by(const Post *post, UserId user) {
    return id_list_contains(post->likes, post->like_count, user);
}

Result post_toggle_like(Post *post, UserId user, bool *liked_now) {
    if (id_list_remove(post->likes, &post->like_count, user)) {
        *liked_now = false;
        return result_ok();
    }
    if (!id_list_add(post->likes, &post->like_count, MAX_LIKES, user)) {
        return result_fail(RESULT_FULL, "Wpis #%u ma już maksymalną liczbę polubień.",
                           post->id.value);
    }
    *liked_now = true;
    return result_ok();
}

Result post_add_comment(Post *post, UserId author, CommentText text, Timestamp at) {
    if (post->comment_count >= MAX_COMMENTS) {
        return result_fail(RESULT_FULL, "Pod wpisem #%u nie zmieści się więcej komentarzy.",
                           post->id.value);
    }
    Comment comment;
    comment.author = author;
    comment.text = text;
    comment.at = at;
    post->comments[post->comment_count++] = comment;
    return result_ok();
}
