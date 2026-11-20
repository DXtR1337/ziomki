#include "infrastructure/file_event_log.h"

#include <time.h>

static const char *name_or_dash(FileEventLog *log, UserId id, User *buffer) {
    if (id.value != 0 && result_is_ok(log->users->by_id(log->users->self, id, buffer))) {
        return buffer->name.value;
    }
    return "-";
}

static void log_publish(void *self, const DomainEvent *event) {
    FileEventLog *log = self;
    if (log->file == NULL) {
        return;
    }
    char when[32] = "?";
    time_t seconds = (time_t)event->at;
    struct tm *local = localtime(&seconds);
    if (local != NULL) {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", local);
    }
    User actor_buffer;
    User subject_buffer;
    const char *actor = name_or_dash(log, event->actor, &actor_buffer);
    const char *subject = name_or_dash(log, event->subject, &subject_buffer);

    switch (event->type) {
    case EVENT_USER_REGISTERED:
        fprintf(log->file, "%s  UserRegistered       %s\n", when, actor);
        break;
    case EVENT_INVITE_SENT:
        fprintf(log->file, "%s  InviteSent           %s -> %s\n", when, actor, subject);
        break;
    case EVENT_FRIENDSHIP_STARTED:
        fprintf(log->file, "%s  FriendshipStarted    %s <-> %s\n", when, actor, subject);
        break;
    case EVENT_FRIENDSHIP_ENDED:
        fprintf(log->file, "%s  FriendshipEnded      %s x %s\n", when, actor, subject);
        break;
    case EVENT_POST_PUBLISHED:
        fprintf(log->file, "%s  PostPublished        #%u by %s\n", when, event->post.value, actor);
        break;
    case EVENT_POST_LIKED:
        fprintf(log->file, "%s  PostLiked            #%u by %s\n", when, event->post.value, actor);
        break;
    case EVENT_POST_COMMENTED:
        fprintf(log->file, "%s  PostCommented        #%u by %s\n", when, event->post.value,
                actor);
        break;
    }
    fflush(log->file);
}

EventPublisher file_event_log_publisher(FileEventLog *log) {
    EventPublisher publisher = {log, log_publish};
    return publisher;
}
