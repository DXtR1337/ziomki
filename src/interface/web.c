#ifdef _WIN32
#define _CRT_RAND_S
#endif

#include "interface/web.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define BAD_SOCKET (-1)
#endif

#define REQUEST_MAX 65536
#define FIELD_MAX 2048
#define TOKEN_HEX 32
#define MAX_SESSIONS 256
#define SUGGESTIONS_SHOWN 5

/* ======================================================================
 * Bufor HTML
 * ==================================================================== */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} Html;

static bool html_reserve(Html *h, size_t extra) {
    if (h->failed) {
        return false;
    }
    size_t needed = h->length + extra + 1;
    if (needed <= h->capacity) {
        return true;
    }
    size_t capacity = h->capacity == 0 ? 16384 : h->capacity;
    while (capacity < needed) {
        capacity *= 2;
    }
    char *data = realloc(h->data, capacity);
    if (data == NULL) {
        h->failed = true;
        return false;
    }
    h->data = data;
    h->capacity = capacity;
    return true;
}

static void html_raw(Html *h, const char *text) {
    size_t n = strlen(text);
    if (html_reserve(h, n)) {
        memcpy(h->data + h->length, text, n + 1);
        h->length += n;
    }
}

static void html_fmt(Html *h, const char *fmt, ...) ZIOMKI_PRINTF(2, 3);
static void html_fmt(Html *h, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n > 0 && html_reserve(h, (size_t)n)) {
        vsnprintf(h->data + h->length, (size_t)n + 1, fmt, args);
        h->length += (size_t)n;
    }
    va_end(args);
}

/* Tekst od użytkownika zawsze przechodzi tędy: bezpieczny w treści i atrybutach. */
static void html_text(Html *h, const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
        case '&': html_raw(h, "&amp;"); break;
        case '<': html_raw(h, "&lt;"); break;
        case '>': html_raw(h, "&gt;"); break;
        case '"': html_raw(h, "&quot;"); break;
        case '\'': html_raw(h, "&#39;"); break;
        default:
            if (html_reserve(h, 1)) {
                h->data[h->length++] = *p;
                h->data[h->length] = '\0';
            }
        }
    }
}

/* ======================================================================
 * Sesje
 * ==================================================================== */

typedef struct {
    bool used;
    char token[TOKEN_HEX + 1];
    UserId user;
    char flash[192];
} Session;

static Session sessions[MAX_SESSIONS];
static size_t next_session_slot = 0;

static bool random_bytes(unsigned char *out, size_t count) {
#ifdef _WIN32
    for (size_t i = 0; i < count; i++) {
        unsigned int value;
        if (rand_s(&value) != 0) {
            return false;
        }
        out[i] = (unsigned char)value;
    }
    return true;
#else
    FILE *source = fopen("/dev/urandom", "rb");
    if (source == NULL) {
        return false;
    }
    size_t read = fread(out, 1, count, source);
    fclose(source);
    return read == count;
#endif
}

static Session *session_find(const char *token) {
    if (token[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].used && strcmp(sessions[i].token, token) == 0) {
            return &sessions[i];
        }
    }
    return NULL;
}

static Session *session_create(UserId user) {
    unsigned char bytes[TOKEN_HEX / 2];
    if (!random_bytes(bytes, sizeof bytes)) {
        return NULL;
    }
    Session *session = &sessions[next_session_slot];
    next_session_slot = (next_session_slot + 1) % MAX_SESSIONS;
    memset(session, 0, sizeof *session);
    for (size_t i = 0; i < sizeof bytes; i++) {
        snprintf(session->token + i * 2, 3, "%02x", bytes[i]);
    }
    session->used = true;
    session->user = user;
    return session;
}

/* ======================================================================
 * Żądanie HTTP
 * ==================================================================== */

typedef struct {
    char method[8];
    char path[512];
    char query[512];
    char sid[TOKEN_HEX + 1];
    const char *body;
    size_t body_length;
    char raw[REQUEST_MAX + 1];
} Request;

static bool header_name_is(const char *line, const char *name) {
    size_t n = strlen(name);
    for (size_t i = 0; i < n; i++) {
        if (line[i] == '\0' || tolower((unsigned char)line[i]) != name[i]) {
            return false;
        }
    }
    return line[n] == ':';
}

/* Wartość nagłówka (bez spacji na początku) albo NULL. Szuka tylko w nagłówkach. */
static const char *find_header(const char *raw, const char *headers_end, const char *name) {
    const char *line = strstr(raw, "\r\n");
    while (line != NULL && line < headers_end) {
        line += 2;
        if (header_name_is(line, name)) {
            const char *value = line + strlen(name) + 1;
            while (*value == ' ') {
                value++;
            }
            return value;
        }
        line = strstr(line, "\r\n");
    }
    return NULL;
}

static void read_cookie_sid(Request *request, const char *headers_end) {
    request->sid[0] = '\0';
    const char *cookie = find_header(request->raw, headers_end, "cookie");
    if (cookie == NULL) {
        return;
    }
    const char *line_end = strstr(cookie, "\r\n");
    const char *p = cookie;
    while (p != NULL && p < line_end) {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        if (strncmp(p, "sid=", 4) == 0) {
            p += 4;
            size_t n = 0;
            while (n < TOKEN_HEX && isxdigit((unsigned char)p[n])) {
                request->sid[n] = p[n];
                n++;
            }
            request->sid[n] = '\0';
            return;
        }
        p = strchr(p, ';');
    }
}

static bool read_request(socket_t client, Request *request) {
    size_t total = 0;
    const char *headers_end = NULL;
    while (total < REQUEST_MAX) {
        int n = recv(client, request->raw + total, (int)(REQUEST_MAX - total), 0);
        if (n <= 0) {
            return false;
        }
        total += (size_t)n;
        request->raw[total] = '\0';
        if (headers_end == NULL) {
            headers_end = strstr(request->raw, "\r\n\r\n");
        }
        if (headers_end == NULL) {
            continue;
        }
        size_t header_length = (size_t)(headers_end - request->raw) + 4;
        const char *length_value = find_header(request->raw, headers_end, "content-length");
        size_t content_length = length_value == NULL ? 0 : strtoul(length_value, NULL, 10);
        if (content_length > REQUEST_MAX - header_length) {
            return false;
        }
        if (total < header_length + content_length) {
            continue;
        }
        request->raw[header_length + content_length] = '\0';
        request->body = request->raw + header_length;
        request->body_length = content_length;

        char target[1024] = "";
        if (sscanf(request->raw, "%7s %1023s", request->method, target) != 2) {
            return false;
        }
        char *question = strchr(target, '?');
        if (question != NULL) {
            *question = '\0';
            snprintf(request->query, sizeof request->query, "%s", question + 1);
        } else {
            request->query[0] = '\0';
        }
        snprintf(request->path, sizeof request->path, "%s", target);
        read_cookie_sid(request, headers_end);
        return true;
    }
    return false;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *src, size_t length, char *out, size_t capacity) {
    size_t o = 0;
    for (size_t i = 0; i < length && o + 1 < capacity; i++) {
        if (src[i] == '+') {
            out[o++] = ' ';
        } else if (src[i] == '%' && i + 2 < length && hex_value(src[i + 1]) >= 0 &&
                   hex_value(src[i + 2]) >= 0) {
            out[o++] = (char)(hex_value(src[i + 1]) * 16 + hex_value(src[i + 2]));
            i += 2;
        } else {
            out[o++] = src[i];
        }
    }
    out[o] = '\0';
}

/* Pole z danych formularza (application/x-www-form-urlencoded) albo z query. */
static bool form_get(const char *data, size_t length, const char *key, char *out,
                     size_t capacity) {
    const char *p = data;
    const char *end = data + length;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (amp == NULL) {
            amp = end;
        }
        const char *eq = memchr(p, '=', (size_t)(amp - p));
        if (eq != NULL) {
            char name[64];
            url_decode(p, (size_t)(eq - p), name, sizeof name);
            if (strcmp(name, key) == 0) {
                url_decode(eq + 1, (size_t)(amp - eq - 1), out, capacity);
                return true;
            }
        }
        p = amp + 1;
    }
    out[0] = '\0';
    return false;
}

static PostId parse_post_id(const char *text) {
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    PostId id = {(end != text && *end == '\0') ? (unsigned)value : 0u};
    return id;
}

/* ======================================================================
 * Odpowiedź HTTP
 * ==================================================================== */

static void send_all(socket_t client, const char *data, size_t length) {
    while (length > 0) {
        int chunk = length > 65536 ? 65536 : (int)length;
        int sent = send(client, data, chunk, 0);
        if (sent <= 0) {
            return;
        }
        data += sent;
        length -= (size_t)sent;
    }
}

static void respond(socket_t client, const char *status, const char *extra_headers,
                    const char *body, size_t body_length) {
    char head[1024];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %u\r\n"
                     "Cache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "Connection: close\r\n"
                     "%s\r\n",
                     status, (unsigned)body_length, extra_headers ? extra_headers : "");
    if (n > 0) {
        send_all(client, head, (size_t)n);
    }
    send_all(client, body, body_length);
}

static void redirect(socket_t client, const char *location, const char *extra_headers) {
    char headers[512];
    snprintf(headers, sizeof headers, "Location: %s\r\n%s", location,
             extra_headers ? extra_headers : "");
    respond(client, "303 See Other", headers, "", 0);
}

/* ======================================================================
 * Widoki
 * ==================================================================== */

static const char *const CSS[] = {
    "*{box-sizing:border-box;margin:0;padding:0}",
    "body{font-family:'Segoe UI Historic','Segoe UI',Helvetica,Arial,sans-serif;",
    "background:#f0f2f5;color:#050505;font-size:15px;line-height:1.34}",
    "a{color:inherit;text-decoration:none}",
    "button{font:inherit;cursor:pointer;border:0;background:none;color:inherit}",
    "input,textarea{font:inherit;color:inherit}",
    ".top{position:sticky;top:0;z-index:10;height:56px;background:#fff;",
    "box-shadow:0 1px 2px rgba(0,0,0,.1);display:grid;grid-template-columns:1fr auto 1fr;",
    "align-items:center;padding:0 16px}",
    ".top-l{display:flex;align-items:center;gap:8px}",
    ".logo{width:40px;height:40px;border-radius:50%;background:#1a6ed8;color:#fff;",
    "display:grid;place-items:center;font:700 30px/1 Helvetica,Arial,sans-serif;",
    "padding-bottom:4px}",
    ".search{display:flex;align-items:center;gap:6px;background:#f0f2f5;",
    "border-radius:50px;height:40px;padding:0 12px;width:240px;color:#65676b}",
    ".search input{border:0;background:none;outline:0;width:100%;font-size:15px}",
    ".search svg{width:16px;height:16px;fill:#65676b;flex:none}",
    ".tabs{display:flex;height:56px}",
    ".tab{width:112px;display:flex;align-items:center;justify-content:center;color:#65676b;",
    "border-bottom:3px solid transparent;margin-top:3px}",
    ".tab:hover{background:#f2f2f2;border-radius:8px}",
    ".tab.on{color:#1a6ed8;border-bottom-color:#1a6ed8;border-radius:0}",
    ".tab svg{width:28px;height:28px;fill:currentColor}",
    ".top-r{display:flex;justify-content:flex-end;align-items:center;gap:8px}",
    ".chip{display:flex;align-items:center;gap:6px;padding:4px 12px 4px 4px;",
    "border-radius:50px;font-weight:600}",
    ".chip:hover,.side a:hover{background:#e4e6eb}",
    ".pill{background:#e4e6eb;border-radius:50px;padding:8px 14px;font-weight:600}",
    ".pill:hover{background:#d8dadf}",
    ".layout{display:grid;grid-template-columns:300px minmax(0,590px) 300px;",
    "justify-content:space-between;gap:32px;padding:16px 16px 40px}",
    ".side{position:sticky;top:72px;align-self:start;max-height:calc(100vh - 88px);",
    "overflow:auto}",
    ".side a,.side .row{display:flex;align-items:center;gap:12px;padding:8px;",
    "border-radius:8px;font-weight:600}",
    ".side h3{color:#65676b;font-size:17px;font-weight:600;padding:12px 8px 6px}",
    ".side hr{border:0;border-top:1px solid #ced0d4;margin:8px}",
    ".ico{width:36px;height:36px;border-radius:50%;display:grid;place-items:center;",
    "background:#e7f0fd;font-size:18px}",
    ".av{flex:none;width:40px;height:40px;border-radius:50%;",
    "background:hsl(var(--h),55%,50%);color:#fff;display:grid;place-items:center;",
    "font-weight:700;font-size:16px}",
    ".av.s{width:36px;height:36px;font-size:15px}",
    ".av.xs{width:32px;height:32px;font-size:13px}",
    ".av.xxs{width:28px;height:28px;font-size:12px}",
    ".card{background:#fff;border-radius:8px;box-shadow:0 1px 2px rgba(0,0,0,.2);",
    "margin-bottom:16px}",
    ".composer{padding:12px 16px}",
    ".composer form{display:flex;gap:8px;align-items:flex-start;flex-wrap:wrap}",
    ".composer textarea{flex:1;background:#f0f2f5;border:0;border-radius:20px;",
    "padding:9px 14px;font-size:17px;resize:none;height:40px;outline:0;color:#050505;",
    "overflow:hidden}",
    ".composer textarea::placeholder{color:#65676b}",
    ".composer textarea:focus{height:96px;border-radius:12px;overflow:auto}",
    ".composer .bar{width:100%;display:flex;justify-content:flex-end;",
    "border-top:1px solid #ced0d4;margin-top:12px;padding-top:8px}",
    ".btn{background:#1a6ed8;color:#fff;font-weight:600;border-radius:6px;padding:7px 16px}",
    ".btn:hover{background:#155ec0}",
    ".btn.gray{background:#e4e6eb;color:#050505}",
    ".btn.gray:hover{background:#d8dadf}",
    ".btn.soft{background:#e7f0fd;color:#1a6ed8}",
    ".btn.soft:hover{background:#dbe7f8}",
    ".btn[disabled]{cursor:default;opacity:.7}",
    ".btn.green{background:#42b72a}",
    ".btn.green:hover{background:#36a420}",
    ".post-h{display:flex;gap:8px;padding:12px 16px 0;align-items:center}",
    ".post-h b{font-size:15px;font-weight:600}",
    ".meta{color:#65676b;font-size:13px}",
    ".post-t{padding:4px 16px 12px;white-space:pre-wrap;overflow-wrap:anywhere}",
    ".post-t.big{font-size:24px;line-height:1.2;padding:10px 16px 16px}",
    ".stats{display:flex;justify-content:space-between;align-items:center;color:#65676b;",
    "padding:0 16px 10px}",
    ".thumb{display:inline-grid;place-items:center;width:18px;height:18px;",
    "border-radius:50%;background:#1a6ed8;font-size:10px;margin-right:6px;",
    "vertical-align:-3px}",
    ".actions{display:flex;margin:0 16px;border-top:1px solid #ced0d4;padding:4px 0}",
    ".actions>*{flex:1}",
    ".act{width:100%;display:flex;justify-content:center;align-items:center;gap:6px;",
    "color:#65676b;font-weight:600;padding:6px;border-radius:4px;cursor:pointer}",
    ".act:hover{background:#f2f2f2}",
    ".act.on{color:#1a6ed8}",
    ".comments{border-top:1px solid #ced0d4;margin:0 16px;padding:8px 0 12px}",
    ".cm{display:flex;gap:6px;margin-top:6px;align-items:flex-start}",
    ".bubble{background:#f0f2f5;border-radius:18px;padding:8px 12px;max-width:100%;",
    "overflow-wrap:anywhere}",
    ".bubble b{font-size:13px;font-weight:600;display:block}",
    ".cm form{flex:1}",
    ".cm input{width:100%;background:#f0f2f5;border:0;border-radius:18px;",
    "padding:8px 12px;outline:0}",
    ".panel{padding:12px 16px}",
    ".panel h2{font-size:17px;margin-bottom:8px}",
    ".flash{background:#fff1f0;color:#b42318;border:1px solid #f5c2c0;padding:12px 16px;",
    "border-radius:8px;margin-bottom:16px}",
    ".person{display:flex;align-items:center;gap:12px;padding:8px;border-radius:8px}",
    ".person .grow{flex:1;font-weight:600;min-width:0;overflow:hidden;text-overflow:ellipsis}",
    ".x{width:28px;height:28px;border-radius:50%;color:#65676b;visibility:hidden}",
    ".person:hover{background:#e4e6eb}.person:hover .x{visibility:visible}",
    ".x:hover{background:#d8dadf}",
    ".dot{position:relative}.dot::after{content:'';position:absolute;right:0;bottom:0;",
    "width:10px;height:10px;border-radius:50%;background:#31a24c;border:2px solid #f0f2f5}",
    ".muted{color:#65676b;padding:4px 8px 8px;font-size:14px}",
    ".empty{padding:32px 24px;text-align:center;color:#65676b}",
    ".foot{color:#65676b;font-size:13px;padding:16px 8px}",
    ".login{min-height:100vh;display:flex;align-items:center;justify-content:center;",
    "gap:72px;padding:40px 20px 120px;flex-wrap:wrap}",
    ".login-l{max-width:500px;padding-top:0}",
    ".wordmark{color:#1a6ed8;font:700 64px/1 Helvetica,Arial,sans-serif;",
    "letter-spacing:-2px;margin:0 0 14px -4px}",
    ".login-l p{font-size:28px;line-height:32px}",
    ".login-card{width:396px;padding:16px 16px 24px;text-align:center;",
    "box-shadow:0 2px 4px rgba(0,0,0,.1),0 8px 16px rgba(0,0,0,.1)}",
    ".login-card input{width:100%;font-size:17px;padding:14px 16px;",
    "border:1px solid #dddfe2;border-radius:6px;margin-bottom:12px}",
    ".login-card input:focus{outline:0;border-color:#1a6ed8;box-shadow:0 0 0 2px #e7f0fd}",
    ".login-card .btn{width:100%;font-size:20px;padding:11px;border-radius:6px}",
    ".login-card hr{border:0;border-top:1px solid #dadde1;margin:20px 0}",
    ".login-card .btn.green{width:auto;font-size:17px;padding:12px 16px}",
    ".login-card .hint{color:#65676b;font-size:13px;margin-top:-4px;margin-bottom:12px}",
    ".login-under{text-align:center;font-size:14px;margin-top:28px}",
    "@media(max-width:1100px){.layout{grid-template-columns:minmax(0,590px) 300px;",
    "justify-content:center}.left{display:none}}",
    "@media(max-width:800px){.layout{grid-template-columns:minmax(0,1fr)}.right{display:none}",
    ".tabs{display:none}.top{grid-template-columns:1fr auto}.search{width:auto}",
    ".chip span:last-child{display:none}}",
    NULL};

static const char *ICON_SEARCH =
    "<svg viewBox=\"0 0 24 24\"><path d=\"M10 3a7 7 0 0 1 5.6 11.2l5.1 5.1-1.4 1.4-5.1-5.1"
    "A7 7 0 1 1 10 3zm0 2a5 5 0 1 0 0 10 5 5 0 0 0 0-10z\"/></svg>";
static const char *ICON_HOME =
    "<svg viewBox=\"0 0 24 24\"><path d=\"M12 2.5 2 11h3v10h5.5v-6h3v6H19V11h3z\"/></svg>";
static const char *ICON_FRIENDS =
    "<svg viewBox=\"0 0 24 24\"><circle cx=\"9\" cy=\"7.5\" r=\"4\"/>"
    "<path d=\"M1 20.5c0-4.4 3.6-7 8-7s8 2.6 8 7z\"/><circle cx=\"17.5\" cy=\"8.5\" r=\"3\"/>"
    "<path d=\"M18 13.6c3 .3 5 2.4 5 5.4v1.5h-4.4c0-2.7-.9-5-2.6-6.6.6-.2 1.3-.3 2-.3z\"/>"
    "</svg>";

typedef struct {
    SocialService *service;
    const WebConfig *config;
} Web;

static void page_open(Html *h, const char *title) {
    html_raw(h, "<!doctype html><html lang=\"pl\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>");
    html_text(h, title);
    html_raw(h, "</title><style>");
    for (const char *const *rule = CSS; *rule != NULL; rule++) {
        html_raw(h, *rule);
    }
    html_raw(h, "</style></head><body>");
}

static void page_close(Html *h) { html_raw(h, "</body></html>"); }

static const char *plural(unsigned n, const char *one, const char *few, const char *many) {
    if (n == 1) {
        return one;
    }
    unsigned last = n % 10;
    unsigned last_two = n % 100;
    if (last >= 2 && last <= 4 && !(last_two >= 12 && last_two <= 14)) {
        return few;
    }
    return many;
}

static Timestamp web_now(Web *web) { return web->service->clock->now(web->service->clock->self); }

static void time_ago(Timestamp now, Timestamp at, char *out, size_t capacity) {
    long long seconds = now - at;
    if (seconds < 60) {
        snprintf(out, capacity, "przed chwilą");
    } else if (seconds < 3600) {
        snprintf(out, capacity, "%lld min", seconds / 60);
    } else if (seconds < 86400) {
        snprintf(out, capacity, "%lld godz.", seconds / 3600);
    } else if (seconds < 7 * 86400) {
        snprintf(out, capacity, "%lld d.", seconds / 86400);
    } else {
        time_t t = (time_t)at;
        struct tm *local = localtime(&t);
        if (local == NULL || strftime(out, capacity, "%d.%m.%Y", local) == 0) {
            snprintf(out, capacity, "dawno");
        }
    }
}

static void avatar(Html *h, const User *user, const char *size) {
    html_fmt(h, "<span class=\"av %s\" style=\"--h:%u\">%c</span>", size,
             (user->id.value * 137u + 200u) % 360u,
             (char)toupper((unsigned char)user->name.value[0]));
}

static bool load(Web *web, UserId id, User *out) {
    if (result_is_ok(social_get_user(web->service, id, out))) {
        return true;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->name.value, sizeof out->name.value, "?");
    return false;
}

static void hidden(Html *h, const char *name, const char *value) {
    html_fmt(h, "<input type=\"hidden\" name=\"%s\" value=\"", name);
    html_text(h, value);
    html_raw(h, "\">");
}

/* Przycisk relacji z drugą osobą: Dodaj / Wysłano / Potwierdź / Znajomi. */
static void relation_button(Html *h, const User *me, const User *other) {
    if (user_id_equals(me->id, other->id)) {
        html_raw(h, "<span class=\"meta\">To ty</span>");
    } else if (user_is_friend(me, other->id)) {
        html_raw(h, "<button class=\"btn gray\" disabled>Znajomi</button>");
    } else if (user_has_invited(me, other->id)) {
        html_raw(h, "<button class=\"btn gray\" disabled>Wysłano</button>");
    } else if (user_has_invite_from(me, other->id)) {
        html_raw(h, "<form method=\"post\" action=\"/accept\">");
        hidden(h, "nick", other->name.value);
        html_raw(h, "<button class=\"btn\">Potwierdź</button></form>");
    } else {
        html_raw(h, "<form method=\"post\" action=\"/invite\">");
        hidden(h, "nick", other->name.value);
        html_raw(h, "<button class=\"btn soft\">Dodaj</button></form>");
    }
}

static void person_row(Html *h, const User *me, const User *other) {
    html_raw(h, "<div class=\"person\">");
    avatar(h, other, "s");
    html_raw(h, "<span class=\"grow\">");
    html_text(h, other->name.value);
    html_raw(h, "</span>");
    relation_button(h, me, other);
    html_raw(h, "</div>");
}

static void likes_summary(Html *h, Web *web, const User *me, const Post *post) {
    unsigned count = (unsigned)post->like_count;
    bool mine = post_is_liked_by(post, me->id);
    const char *names[2];
    User buffers[2];
    unsigned shown = 0;
    if (mine) {
        names[shown++] = "Ty";
    }
    for (size_t i = 0; i < post->like_count && shown < 2; i++) {
        if (user_id_equals(post->likes[i], me->id)) {
            continue;
        }
        load(web, post->likes[i], &buffers[shown]);
        names[shown] = buffers[shown].name.value;
        shown++;
    }
    html_raw(h, "<span><span class=\"thumb\">👍</span>");
    if (count == 1) {
        html_text(h, names[0]);
    } else if (count == 2) {
        html_text(h, names[0]);
        html_raw(h, " i ");
        html_text(h, names[1]);
    } else {
        unsigned others = count - 2;
        html_text(h, names[0]);
        html_raw(h, ", ");
        html_text(h, names[1]);
        html_fmt(h, " i %u %s", others,
                 plural(others, "inna osoba", "inne osoby", "innych osób"));
    }
    html_raw(h, "</span>");
}

static void render_post(Html *h, Web *web, const User *me, const Post *post, Timestamp now) {
    User author;
    load(web, post->author, &author);
    char when[32];
    time_ago(now, post->published_at, when, sizeof when);

    html_fmt(h, "<article class=\"card\" id=\"p%u\"><div class=\"post-h\">", post->id.value);
    avatar(h, &author, "");
    html_raw(h, "<div><b>");
    html_text(h, author.name.value);
    html_fmt(h, "</b><div class=\"meta\">%s · 👥</div></div></div>", when);

    bool big = utf8_char_count(post->content.value) <= 85 && strchr(post->content.value, '\n') == NULL;
    html_fmt(h, "<p class=\"post-t%s\">", big ? " big" : "");
    html_text(h, post->content.value);
    html_raw(h, "</p>");

    if (post->like_count > 0 || post->comment_count > 0) {
        html_raw(h, "<div class=\"stats\">");
        if (post->like_count > 0) {
            likes_summary(h, web, me, post);
        } else {
            html_raw(h, "<span></span>");
        }
        if (post->comment_count > 0) {
            unsigned n = (unsigned)post->comment_count;
            html_fmt(h, "<label for=\"c%u\" style=\"cursor:pointer\">%u %s</label>",
                     post->id.value, n, plural(n, "komentarz", "komentarze", "komentarzy"));
        }
        html_raw(h, "</div>");
    }

    bool liked = post_is_liked_by(post, me->id);
    html_fmt(h,
             "<div class=\"actions\"><form method=\"post\" action=\"/like\">"
             "<input type=\"hidden\" name=\"id\" value=\"%u\">"
             "<button class=\"act%s\">👍 Lubię to!</button></form>"
             "<label class=\"act\" for=\"c%u\">💬 Komentarz</label></div>",
             post->id.value, liked ? " on" : "", post->id.value);

    html_raw(h, "<div class=\"comments\">");
    for (size_t i = 0; i < post->comment_count; i++) {
        const Comment *comment = &post->comments[i];
        User commenter;
        load(web, comment->author, &commenter);
        html_raw(h, "<div class=\"cm\">");
        avatar(h, &commenter, "xs");
        html_raw(h, "<div class=\"bubble\"><b>");
        html_text(h, commenter.name.value);
        html_raw(h, "</b>");
        html_text(h, comment->text.value);
        html_raw(h, "</div></div>");
    }
    html_raw(h, "<div class=\"cm\">");
    avatar(h, me, "xs");
    html_fmt(h,
             "<form method=\"post\" action=\"/comment\">"
             "<input type=\"hidden\" name=\"id\" value=\"%u\">"
             "<input id=\"c%u\" name=\"text\" maxlength=\"%d\" autocomplete=\"off\" "
             "placeholder=\"Napisz komentarz…\"></form></div></div></article>",
             post->id.value, post->id.value, COMMENT_MAX_CHARS);
}

static void render_top(Html *h, const User *me, const char *query) {
    html_raw(h, "<header class=\"top\"><div class=\"top-l\"><a class=\"logo\" href=\"/\" "
                "aria-label=\"Ziomki\">z</a><form class=\"search\" action=\"/\" method=\"get\">");
    html_raw(h, ICON_SEARCH);
    html_raw(h, "<input name=\"q\" placeholder=\"Szukaj w Ziomkach\" value=\"");
    html_text(h, query);
    html_raw(h, "\"></form></div><nav class=\"tabs\"><a class=\"tab on\" href=\"/\" "
                "title=\"Strona główna\">");
    html_raw(h, ICON_HOME);
    html_raw(h, "</a><a class=\"tab\" href=\"#kontakty\" title=\"Znajomi\">");
    html_raw(h, ICON_FRIENDS);
    html_raw(h, "</a></nav><div class=\"top-r\"><a class=\"chip\" href=\"/\">");
    avatar(h, me, "xxs");
    html_raw(h, "<span>");
    html_text(h, me->name.value);
    html_raw(h, "</span></a><form method=\"post\" action=\"/logout\">"
                "<button class=\"pill\">Wyloguj</button></form></div></header>");
}

static void render_left(Html *h, const User *me) {
    html_raw(h, "<aside class=\"side left\"><a href=\"/\">");
    avatar(h, me, "s");
    html_text(h, me->name.value);
    html_fmt(h,
             "</a><a href=\"#kontakty\"><span class=\"ico\">👥</span>Znajomi (%u)</a>"
             "<a href=\"/\"><span class=\"ico\">🕘</span>Najnowsze</a>"
             "<a href=\"/?q=\"><span class=\"ico\">🔎</span>Znajdź znajomych</a>"
             "<hr><p class=\"foot\">Prywatność · Warunki · Ziomki © 2026<br>"
             "Napisane w C11, architektura DDD</p></aside>",
             (unsigned)me->friend_count);
}

static void render_right(Html *h, Web *web, const User *me) {
    html_raw(h, "<aside class=\"side right\">");
    User other;
    if (me->incoming_count > 0) {
        html_raw(h, "<h3>Zaproszenia do znajomych</h3>");
        for (size_t i = 0; i < me->incoming_count; i++) {
            load(web, me->incoming[i], &other);
            person_row(h, me, &other);
        }
        html_raw(h, "<hr>");
    }

    UserId suggested[SUGGESTIONS_SHOWN];
    size_t suggested_count = 0;
    social_suggestions(web->service, me->id, suggested, SUGGESTIONS_SHOWN, &suggested_count);
    if (suggested_count > 0) {
        html_raw(h, "<h3>Osoby, które możesz znać</h3>");
        for (size_t i = 0; i < suggested_count; i++) {
            load(web, suggested[i], &other);
            person_row(h, me, &other);
        }
        html_raw(h, "<hr>");
    }

    html_raw(h, "<h3 id=\"kontakty\">Kontakty</h3>");
    if (me->friend_count == 0) {
        html_raw(h, "<p class=\"muted\">Nie masz jeszcze znajomych. Dodaj kogoś z listy "
                    "albo wyszukaj po nicku.</p>");
    }
    for (size_t i = 0; i < me->friend_count; i++) {
        load(web, me->friends[i], &other);
        html_raw(h, "<div class=\"person\"><span class=\"dot\">");
        avatar(h, &other, "s");
        html_raw(h, "</span><span class=\"grow\">");
        html_text(h, other.name.value);
        html_raw(h, "</span><form method=\"post\" action=\"/unfriend\">");
        hidden(h, "nick", other.name.value);
        html_raw(h, "<button class=\"x\" title=\"Usuń ze znajomych\">✕</button></form></div>");
    }
    html_raw(h, "</aside>");
}

static void render_search(Html *h, Web *web, const User *me, const char *query) {
    html_raw(h, "<div class=\"card panel\"><h2>Szukaj znajomych</h2>");
    if (query[0] == '\0') {
        html_raw(h, "<p class=\"meta\">Wpisz nick w polu wyszukiwania u góry.</p></div>");
        return;
    }
    User found;
    Result result = social_find_user(web->service, query, &found);
    if (result_is_ok(result)) {
        person_row(h, me, &found);
    } else {
        html_raw(h, "<p class=\"meta\">Nie ma nikogo o nicku „");
        html_text(h, query);
        html_raw(h, "”.</p>");
    }
    html_raw(h, "</div>");
}

static void render_home(Html *h, Web *web, Session *session, const User *me, bool searching,
                        const char *query) {
    page_open(h, "Ziomki");
    render_top(h, me, query);
    html_raw(h, "<main class=\"layout\">");
    render_left(h, me);
    html_raw(h, "<section class=\"feed\">");

    if (session->flash[0] != '\0') {
        html_raw(h, "<div class=\"flash\">");
        html_text(h, session->flash);
        html_raw(h, "</div>");
        session->flash[0] = '\0';
    }
    if (searching) {
        render_search(h, web, me, query);
    }

    html_raw(h, "<div class=\"card composer\"><form method=\"post\" action=\"/post\">");
    avatar(h, me, "");
    html_fmt(h, "<textarea name=\"text\" maxlength=\"%d\" placeholder=\"Co słychać, ",
             POST_MAX_CHARS);
    html_text(h, me->name.value);
    html_raw(h, "?\"></textarea><div class=\"bar\"><button class=\"btn\">Opublikuj</button>"
                "</div></form></div>");

    PostHeader headers[FEED_PAGE_SIZE];
    size_t count = 0;
    social_feed(web->service, me->id, headers, FEED_PAGE_SIZE, &count);
    if (count == 0) {
        html_raw(h, "<div class=\"card empty\">Tu pojawią się twoje wpisy i wpisy znajomych."
                    "<br>Napisz pierwszy powyżej.</div>");
    }
    Timestamp now = web_now(web);
    static Post post;
    for (size_t i = 0; i < count; i++) {
        if (result_is_ok(social_view_post(web->service, me->id, headers[i].id, &post))) {
            render_post(h, web, me, &post, now);
        }
    }
    html_raw(h, "</section>");
    render_right(h, web, me);
    html_raw(h, "</main>");
    page_close(h);
}

static void render_login(Html *h, const char *error) {
    page_open(h, "Ziomki – zaloguj się lub zarejestruj");
    html_raw(h,
             "<div class=\"login\"><div class=\"login-l\"><div class=\"wordmark\">ziomki</div>"
             "<p>Ziomki pomagają ci być w kontakcie z ludźmi, których znasz.</p></div>"
             "<div><div class=\"card login-card\">");
    if (error != NULL) {
        html_raw(h, "<div class=\"flash\">");
        html_text(h, error);
        html_raw(h, "</div>");
    }
    html_raw(h,
             "<form method=\"post\" action=\"/login\">"
             "<input name=\"nick\" placeholder=\"Nick\" autofocus autocomplete=\"username\">"
             "<button class=\"btn\">Zaloguj się</button></form><hr>"
             "<form method=\"post\" action=\"/register\">"
             "<input name=\"nick\" placeholder=\"Nowy nick (3–24 znaki: a-z, 0-9, _)\">"
             "<button class=\"btn green\">Utwórz nowe konto</button></form></div>"
             "<p class=\"login-under\"><b>Wersja demonstracyjna:</b> bez haseł, "
             "dane lokalnie na tym komputerze.</p></div></div>");
    page_close(h);
}

/* ======================================================================
 * Routing
 * ==================================================================== */

static void send_html(socket_t client, const char *status, Html *h, const char *headers) {
    if (h->failed || h->data == NULL) {
        respond(client, "500 Internal Server Error", NULL, "Brak pamięci", 13);
        return;
    }
    respond(client, status, headers, h->data, h->length);
}

static void persist(Web *web) {
    if (web->config->persist != NULL) {
        web->config->persist(web->config->persist_context);
    }
}

static void finish_action(Web *web, socket_t client, Session *session, Result result,
                          const char *location) {
    if (result_is_ok(result)) {
        persist(web);
    } else {
        snprintf(session->flash, sizeof session->flash, "%s", result.message);
    }
    redirect(client, result_is_ok(result) ? location : "/", NULL);
}

static void handle(Web *web, socket_t client, Request *request) {
    Html h = {NULL, 0, 0, false};
    bool is_get = strcmp(request->method, "GET") == 0;
    bool is_post = strcmp(request->method, "POST") == 0;

    Session *session = session_find(request->sid);
    User me;
    if (session != NULL && !result_is_ok(social_get_user(web->service, session->user, &me))) {
        session->used = false;
        session = NULL;
    }

    char field[FIELD_MAX];
    char extra[FIELD_MAX];

    if (is_get && strcmp(request->path, "/") == 0) {
        if (session == NULL) {
            render_login(&h, NULL);
        } else {
            bool searching = form_get(request->query, strlen(request->query), "q", field,
                                      sizeof field);
            render_home(&h, web, session, &me, searching, field);
        }
        send_html(client, "200 OK", &h, NULL);
    } else if (is_post &&
               (strcmp(request->path, "/login") == 0 || strcmp(request->path, "/register") == 0)) {
        form_get(request->body, request->body_length, "nick", field, sizeof field);
        bool registering = strcmp(request->path, "/register") == 0;
        UserId id = {0};
        Result result;
        if (registering) {
            result = social_register(web->service, field, &id);
        } else {
            result = social_find_user(web->service, field, &me);
            id = me.id;
        }
        Session *created = result_is_ok(result) ? session_create(id) : NULL;
        if (created == NULL) {
            render_login(&h, result_is_ok(result) ? "Nie udało się otworzyć sesji." : result.message);
            send_html(client, "200 OK", &h, NULL);
        } else {
            if (registering) {
                persist(web);
            }
            char cookie[128];
            snprintf(cookie, sizeof cookie,
                     "Set-Cookie: sid=%s; Path=/; HttpOnly; SameSite=Lax\r\n", created->token);
            redirect(client, "/", cookie);
        }
    } else if (is_post && strcmp(request->path, "/logout") == 0) {
        if (session != NULL) {
            session->used = false;
        }
        redirect(client, "/", "Set-Cookie: sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
    } else if (is_post && session == NULL) {
        redirect(client, "/", NULL);
    } else if (is_post && strcmp(request->path, "/post") == 0) {
        form_get(request->body, request->body_length, "text", field, sizeof field);
        PostId id;
        finish_action(web, client, session, social_publish(web->service, me.id, field, &id), "/");
    } else if (is_post && strcmp(request->path, "/like") == 0) {
        form_get(request->body, request->body_length, "id", field, sizeof field);
        PostId id = parse_post_id(field);
        bool liked;
        char location[32];
        snprintf(location, sizeof location, "/#p%u", id.value);
        finish_action(web, client, session, social_toggle_like(web->service, me.id, id, &liked),
                      location);
    } else if (is_post && strcmp(request->path, "/comment") == 0) {
        form_get(request->body, request->body_length, "id", field, sizeof field);
        form_get(request->body, request->body_length, "text", extra, sizeof extra);
        PostId id = parse_post_id(field);
        char location[32];
        snprintf(location, sizeof location, "/#p%u", id.value);
        finish_action(web, client, session, social_comment(web->service, me.id, id, extra),
                      location);
    } else if (is_post && strcmp(request->path, "/invite") == 0) {
        form_get(request->body, request->body_length, "nick", field, sizeof field);
        bool became;
        finish_action(web, client, session, social_invite(web->service, me.id, field, &became),
                      "/");
    } else if (is_post && strcmp(request->path, "/accept") == 0) {
        form_get(request->body, request->body_length, "nick", field, sizeof field);
        finish_action(web, client, session, social_accept(web->service, me.id, field), "/");
    } else if (is_post && strcmp(request->path, "/unfriend") == 0) {
        form_get(request->body, request->body_length, "nick", field, sizeof field);
        finish_action(web, client, session, social_unfriend(web->service, me.id, field), "/");
    } else {
        page_open(&h, "Nie znaleziono");
        html_raw(&h, "<div class=\"card empty\" style=\"max-width:500px;margin:80px auto\">"
                     "Tej strony nie ma. <a href=\"/\" style=\"color:#1a6ed8\">"
                     "Wróć na stronę główną</a></div>");
        page_close(&h);
        send_html(client, "404 Not Found", &h, NULL);
    }
    free(h.data);
}

/* ======================================================================
 * Pętla serwera
 * ==================================================================== */

static void set_receive_timeout(socket_t client, int seconds) {
#ifdef _WIN32
    DWORD timeout = (DWORD)seconds * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
#else
    struct timeval timeout = {seconds, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#endif
}

int web_run(const WebConfig *config) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "! Nie udało się uruchomić Winsock.\n");
        return 1;
    }
#endif
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == BAD_SOCKET) {
        fprintf(stderr, "! Nie udało się utworzyć gniazda.\n");
        return 1;
    }
#ifndef _WIN32
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(config->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&address, sizeof address) != 0 ||
        listen(server, 16) != 0) {
        fprintf(stderr, "! Port %u jest zajęty albo niedostępny.\n", config->port);
        CLOSE_SOCKET(server);
        return 1;
    }
    printf("Ziomki działają: http://localhost:%u  (Ctrl+C kończy)\n", config->port);
    fflush(stdout);

    Web web = {config->service, config};
    static Request request;
    for (;;) {
        socket_t client = accept(server, NULL, NULL);
        if (client == BAD_SOCKET) {
            continue;
        }
        set_receive_timeout(client, 5);
        request.method[0] = '\0';
        if (read_request(client, &request)) {
            handle(&web, client, &request);
        }
        CLOSE_SOCKET(client);
    }
}
