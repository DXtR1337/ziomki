#ifndef ZIOMKI_INFRASTRUCTURE_FILE_EVENT_LOG_H
#define ZIOMKI_INFRASTRUCTURE_FILE_EVENT_LOG_H

#include <stdio.h>

#include "domain/ports.h"

/* Adapter EventPublisher: dopisuje zdarzenia domenowe do pliku tekstowego. */
typedef struct {
    FILE *file;
    UserRepository *users;
} FileEventLog;

EventPublisher file_event_log_publisher(FileEventLog *log);

#endif
