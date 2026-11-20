#ifndef ZIOMKI_DOMAIN_VALUE_OBJECTS_H
#define ZIOMKI_DOMAIN_VALUE_OBJECTS_H

/*
 * Obiekty wartości: niezmienne, porównywane po wartości, poprawne od chwili
 * utworzenia. Jedyna droga do Username, PostContent i CommentText prowadzi
 * przez funkcje *_create, które pilnują reguł.
 */

#include <stdbool.h>
#include <stddef.h>

#include "shared/result.h"

#define USERNAME_MIN_CHARS 3
#define USERNAME_MAX_CHARS 24
#define POST_MAX_CHARS 280
#define COMMENT_MAX_CHARS 140
#define UTF8_BYTES_FOR(chars) ((chars) * 4 + 1)

typedef long long Timestamp;

typedef struct { unsigned value; } UserId;
typedef struct { unsigned value; } PostId;

typedef struct { char value[USERNAME_MAX_CHARS + 1]; } Username;
typedef struct { char value[UTF8_BYTES_FOR(POST_MAX_CHARS)]; } PostContent;
typedef struct { char value[UTF8_BYTES_FOR(COMMENT_MAX_CHARS)]; } CommentText;

bool user_id_equals(UserId a, UserId b);
bool post_id_equals(PostId a, PostId b);

Result username_create(const char *raw, Username *out);
Result post_content_create(const char *raw, PostContent *out);
Result comment_text_create(const char *raw, CommentText *out);

/* Liczba znaków UTF-8 albo (size_t)-1, gdy bajty nie są poprawnym UTF-8. */
size_t utf8_char_count(const char *text);

#endif
