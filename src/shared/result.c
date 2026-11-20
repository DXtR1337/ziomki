#include "shared/result.h"

#include <stdarg.h>
#include <stdio.h>

Result result_ok(void) {
    Result result = {RESULT_OK, ""};
    return result;
}

Result result_fail(ResultCode code, const char *fmt, ...) {
    Result result;
    result.code = code;
    va_list args;
    va_start(args, fmt);
    vsnprintf(result.message, sizeof result.message, fmt, args);
    va_end(args);
    return result;
}
