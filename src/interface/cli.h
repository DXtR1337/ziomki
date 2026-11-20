#ifndef ZIOMKI_INTERFACE_CLI_H
#define ZIOMKI_INTERFACE_CLI_H

#include <stdbool.h>
#include <stdio.h>

#include "application/social_service.h"

typedef void (*CliPersistFn)(void *context);

typedef struct {
    SocialService *service;
    FILE *in;
    FILE *out;
    bool echo_input;          /* przy wejściu z pliku lub potoku pokazuje wpisane polecenia */
    CliPersistFn persist;     /* wołane po każdym udanym poleceniu, które zmienia dane */
    void *persist_context;
} CliConfig;

int cli_run(const CliConfig *config);

#endif
