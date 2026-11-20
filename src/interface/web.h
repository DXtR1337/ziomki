#ifndef ZIOMKI_INTERFACE_WEB_H
#define ZIOMKI_INTERFACE_WEB_H

/*
 * Interfejs webowy: jednowątkowy serwer HTTP/1.1 na gniazdach BSD/Winsock,
 * HTML generowany w C. Nasłuchuje tylko na 127.0.0.1.
 */

#include "application/social_service.h"

typedef void (*WebPersistFn)(void *context);

typedef struct {
    SocialService *service;
    unsigned short port;
    WebPersistFn persist;
    void *persist_context;
} WebConfig;

int web_run(const WebConfig *config);

#endif
