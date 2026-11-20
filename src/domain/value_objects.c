#include "domain/value_objects.h"

#include <ctype.h>
#include <string.h>

#define UTF8_INVALID ((size_t)-1)

bool user_id_equals(UserId a, UserId b) { return a.value == b.value; }

bool post_id_equals(PostId a, PostId b) { return a.value == b.value; }

size_t utf8_char_count(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    size_t count = 0;
    while (*p != '\0') {
        size_t length;
        if (*p < 0x80) {
            length = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            length = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            length = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            length = 4;
        } else {
            return UTF8_INVALID;
        }
        for (size_t i = 1; i < length; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                return UTF8_INVALID;
            }
        }
        p += length;
        count++;
    }
    return count;
}

static Result text_create(const char *raw, char *out, size_t capacity, size_t max_chars,
                          const char *label) {
    if (raw == NULL) {
        return result_fail(RESULT_INVALID, "%s jest pusty.", label);
    }
    while (*raw != '\0' && isspace((unsigned char)*raw)) {
        raw++;
    }
    size_t length = strlen(raw);
    while (length > 0 && isspace((unsigned char)raw[length - 1])) {
        length--;
    }
    if (length == 0) {
        return result_fail(RESULT_INVALID, "%s jest pusty.", label);
    }
    if (length >= capacity) {
        return result_fail(RESULT_INVALID, "%s może mieć najwyżej %u znaków.", label,
                           (unsigned)max_chars);
    }
    memcpy(out, raw, length);
    out[length] = '\0';

    size_t chars = utf8_char_count(out);
    if (chars == UTF8_INVALID) {
        out[0] = '\0';
        return result_fail(RESULT_INVALID, "%s zawiera niepoprawne znaki.", label);
    }
    if (chars > max_chars) {
        out[0] = '\0';
        return result_fail(RESULT_INVALID, "%s może mieć najwyżej %u znaków.", label,
                           (unsigned)max_chars);
    }
    return result_ok();
}

Result username_create(const char *raw, Username *out) {
    size_t length = raw == NULL ? 0 : strlen(raw);
    if (length < USERNAME_MIN_CHARS || length > USERNAME_MAX_CHARS) {
        return result_fail(RESULT_INVALID, "Nick musi mieć od %d do %d znaków.",
                           USERNAME_MIN_CHARS, USERNAME_MAX_CHARS);
    }
    Username name;
    for (size_t i = 0; i < length; i++) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!allowed) {
            return result_fail(RESULT_INVALID,
                               "Nick może zawierać tylko litery a-z, cyfry i podkreślnik.");
        }
        name.value[i] = c;
    }
    name.value[length] = '\0';
    *out = name;
    return result_ok();
}

Result post_content_create(const char *raw, PostContent *out) {
    return text_create(raw, out->value, sizeof out->value, POST_MAX_CHARS, "Wpis");
}

Result comment_text_create(const char *raw, CommentText *out) {
    return text_create(raw, out->value, sizeof out->value, COMMENT_MAX_CHARS, "Komentarz");
}
