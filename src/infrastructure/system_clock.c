#include "infrastructure/system_clock.h"

#include <time.h>

static Timestamp system_now(void *self) {
    (void)self;
    return (Timestamp)time(NULL);
}

Clock system_clock(void) {
    Clock clock = {NULL, system_now};
    return clock;
}
