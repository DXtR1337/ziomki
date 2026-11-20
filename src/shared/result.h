#ifndef ZIOMKI_SHARED_RESULT_H
#define ZIOMKI_SHARED_RESULT_H

#include <stdbool.h>

typedef enum {
    RESULT_OK = 0,
    RESULT_INVALID,
    RESULT_NOT_FOUND,
    RESULT_CONFLICT,
    RESULT_FULL,
    RESULT_IO
} ResultCode;

typedef struct {
    ResultCode code;
    char message[192];
} Result;

#if defined(__GNUC__) || defined(__clang__)
#define ZIOMKI_PRINTF(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define ZIOMKI_PRINTF(fmt_index, args_index)
#endif

Result result_ok(void);
Result result_fail(ResultCode code, const char *fmt, ...) ZIOMKI_PRINTF(2, 3);

static inline bool result_is_ok(Result result) { return result.code == RESULT_OK; }

#define TRY(expr)                            \
    do {                                     \
        Result try_result_ = (expr);         \
        if (try_result_.code != RESULT_OK) { \
            return try_result_;              \
        }                                    \
    } while (0)

#endif
