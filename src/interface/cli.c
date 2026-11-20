#include "interface/cli.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLI_LINE_MAX 2048

typedef struct {
    const CliConfig *config;
    FILE *out;
    bool logged_in;
    UserId me;
} Cli;

typedef Result (*CommandFn)(Cli *cli, char *args);

typedef struct {
    const char *name;
    CommandFn run;
    bool needs_login;
    bool mutates;
} Command;

/* --- pomocnicze --- */

static char *next_token(char **cursor) {
    char *p = *cursor;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    if (*p != '\0') {
        *p++ = '\0';
    }
    *cursor = p;
    return start;
}

static Result require(const char *arg, const char *usage) {
    if (arg == NULL || *arg == '\0') {
        return result_fail(RESULT_INVALID, "Użycie: %s", usage);
    }
    return result_ok();
}

static Result parse_post_id(const char *token, PostId *out) {
    TRY(require(token, "podaj numer wpisu, np. 3"));
    if (*token == '#') {
        token++;
    }
    char *end = NULL;
    unsigned long long value = strtoull(token, &end, 10);
    if (end == token || *end != '\0' || value == 0 || value > 0xFFFFFFFFull) {
        return result_fail(RESULT_INVALID, "„%s” nie jest numerem wpisu.", token);
    }
    out->value = (unsigned)value;
    return result_ok();
}

static const char *name_of(Cli *cli, UserId id, User *buffer) {
    if (result_is_ok(social_get_user(cli->config->service, id, buffer))) {
        return buffer->name.value;
    }
    return "(konto usunięte)";
}

static void format_time(Timestamp at, char *buffer, size_t capacity) {
    time_t seconds = (time_t)at;
    struct tm *local = localtime(&seconds);
    if (local == NULL || strftime(buffer, capacity, "%d.%m.%Y %H:%M", local) == 0) {
        snprintf(buffer, capacity, "?");
    }
}

static void print_user_list(Cli *cli, const char *title, const UserId *ids, size_t count) {
    User buffer;
    fprintf(cli->out, "%s (%u):", title, (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        fprintf(cli->out, "%s %s", i == 0 ? "" : ",", name_of(cli, ids[i], &buffer));
    }
    fprintf(cli->out, count == 0 ? " brak\n" : "\n");
}

static void print_post(Cli *cli, const Post *post) {
    User buffer;
    char when[32];
    format_time(post->published_at, when, sizeof when);
    fprintf(cli->out, "\n#%u  %s · %s\n", post->id.value, name_of(cli, post->author, &buffer),
            when);
    fprintf(cli->out, "  %s\n", post->content.value);
    fprintf(cli->out, "  Polubienia: %u", (unsigned)post->like_count);
    if (post->like_count > 0) {
        fprintf(cli->out, " (");
        for (size_t i = 0; i < post->like_count; i++) {
            fprintf(cli->out, "%s%s", i == 0 ? "" : ", ", name_of(cli, post->likes[i], &buffer));
        }
        fprintf(cli->out, ")");
    }
    fprintf(cli->out, "  ·  Komentarze: %u\n", (unsigned)post->comment_count);
    for (size_t i = 0; i < post->comment_count; i++) {
        const Comment *comment = &post->comments[i];
        fprintf(cli->out, "    └ %s: %s\n", name_of(cli, comment->author, &buffer),
                comment->text.value);
    }
}

/* --- polecenia --- */

static Result cmd_help(Cli *cli, char *args) {
    (void)args;
    fputs("Konto\n"
          "  rejestracja <nick>      załóż konto i od razu się zaloguj\n"
          "  login <nick>            zaloguj się\n"
          "  wyloguj\n"
          "Znajomi\n"
          "  zapros <nick>           wyślij zaproszenie\n"
          "  akceptuj <nick>         przyjmij zaproszenie\n"
          "  usun <nick>             usuń ze znajomych\n"
          "  zaproszenia             otrzymane i wysłane zaproszenia\n"
          "  znajomi                 lista znajomych\n"
          "Wpisy\n"
          "  wpis <tekst>            opublikuj (do 280 znaków)\n"
          "  tablica                 twoje wpisy i wpisy znajomych\n"
          "  pokaz <nr>              jeden wpis z komentarzami\n"
          "  lubie <nr>              polub albo cofnij polubienie\n"
          "  komentuj <nr> <tekst>   skomentuj (do 140 znaków)\n"
          "Inne\n"
          "  pomoc, koniec\n",
          cli->out);
    return result_ok();
}

static Result cmd_register(Cli *cli, char *args) {
    const char *nick = next_token(&args);
    TRY(require(nick, "rejestracja <nick>"));
    UserId id;
    TRY(social_register(cli->config->service, nick, &id));
    User user;
    TRY(social_get_user(cli->config->service, id, &user));
    cli->logged_in = true;
    cli->me = id;
    fprintf(cli->out, "Konto %s założone, sesja otwarta.\n", user.name.value);
    return result_ok();
}

static Result cmd_login(Cli *cli, char *args) {
    const char *nick = next_token(&args);
    TRY(require(nick, "login <nick>"));
    User user;
    TRY(social_find_user(cli->config->service, nick, &user));
    cli->logged_in = true;
    cli->me = user.id;
    fprintf(cli->out, "Cześć, %s.", user.name.value);
    if (user.incoming_count > 0) {
        fprintf(cli->out, " Oczekujące zaproszenia: %u (wpisz: zaproszenia).",
                (unsigned)user.incoming_count);
    }
    fprintf(cli->out, "\n");
    return result_ok();
}

static Result cmd_logout(Cli *cli, char *args) {
    (void)args;
    cli->logged_in = false;
    fprintf(cli->out, "Wylogowano.\n");
    return result_ok();
}

static Result cmd_invite(Cli *cli, char *args) {
    const char *nick = next_token(&args);
    TRY(require(nick, "zapros <nick>"));
    bool became_friends = false;
    TRY(social_invite(cli->config->service, cli->me, nick, &became_friends));
    if (became_friends) {
        fprintf(cli->out, "Zaproszenia się spotkały: ty i %s jesteście znajomymi.\n", nick);
    } else {
        fprintf(cli->out, "Zaproszenie do %s wysłane.\n", nick);
    }
    return result_ok();
}

static Result cmd_accept(Cli *cli, char *args) {
    const char *nick = next_token(&args);
    TRY(require(nick, "akceptuj <nick>"));
    TRY(social_accept(cli->config->service, cli->me, nick));
    fprintf(cli->out, "Ty i %s jesteście znajomymi.\n", nick);
    return result_ok();
}

static Result cmd_unfriend(Cli *cli, char *args) {
    const char *nick = next_token(&args);
    TRY(require(nick, "usun <nick>"));
    TRY(social_unfriend(cli->config->service, cli->me, nick));
    fprintf(cli->out, "%s nie jest już na liście znajomych.\n", nick);
    return result_ok();
}

static Result cmd_invites(Cli *cli, char *args) {
    (void)args;
    User me;
    TRY(social_get_user(cli->config->service, cli->me, &me));
    print_user_list(cli, "Otrzymane", me.incoming, me.incoming_count);
    print_user_list(cli, "Wysłane", me.outgoing, me.outgoing_count);
    return result_ok();
}

static Result cmd_friends(Cli *cli, char *args) {
    (void)args;
    User me;
    TRY(social_get_user(cli->config->service, cli->me, &me));
    print_user_list(cli, "Znajomi", me.friends, me.friend_count);
    return result_ok();
}

static Result cmd_post(Cli *cli, char *args) {
    TRY(require(args, "wpis <tekst>"));
    PostId id;
    TRY(social_publish(cli->config->service, cli->me, args, &id));
    fprintf(cli->out, "Opublikowano wpis #%u.\n", id.value);
    return result_ok();
}

static Result cmd_feed(Cli *cli, char *args) {
    (void)args;
    PostHeader headers[FEED_PAGE_SIZE];
    size_t count = 0;
    TRY(social_feed(cli->config->service, cli->me, headers, FEED_PAGE_SIZE, &count));
    if (count == 0) {
        fprintf(cli->out, "Tablica jest pusta. Napisz coś: wpis <tekst>\n");
        return result_ok();
    }
    static Post post;
    for (size_t i = 0; i < count; i++) {
        if (result_is_ok(social_view_post(cli->config->service, cli->me, headers[i].id, &post))) {
            print_post(cli, &post);
        }
    }
    fprintf(cli->out, "\n");
    return result_ok();
}

static Result cmd_show(Cli *cli, char *args) {
    PostId id;
    TRY(parse_post_id(next_token(&args), &id));
    static Post post;
    TRY(social_view_post(cli->config->service, cli->me, id, &post));
    print_post(cli, &post);
    fprintf(cli->out, "\n");
    return result_ok();
}

static Result cmd_like(Cli *cli, char *args) {
    PostId id;
    TRY(parse_post_id(next_token(&args), &id));
    bool liked_now = false;
    TRY(social_toggle_like(cli->config->service, cli->me, id, &liked_now));
    fprintf(cli->out, liked_now ? "Lubisz wpis #%u.\n" : "Cofnięto polubienie wpisu #%u.\n",
            id.value);
    return result_ok();
}

static Result cmd_comment(Cli *cli, char *args) {
    PostId id;
    TRY(parse_post_id(next_token(&args), &id));
    TRY(require(args, "komentuj <nr> <tekst>"));
    TRY(social_comment(cli->config->service, cli->me, id, args));
    fprintf(cli->out, "Dodano komentarz pod wpisem #%u.\n", id.value);
    return result_ok();
}

static const Command COMMANDS[] = {
    {"pomoc", cmd_help, false, false},
    {"rejestracja", cmd_register, false, true},
    {"login", cmd_login, false, false},
    {"wyloguj", cmd_logout, true, false},
    {"zapros", cmd_invite, true, true},
    {"akceptuj", cmd_accept, true, true},
    {"usun", cmd_unfriend, true, true},
    {"zaproszenia", cmd_invites, true, false},
    {"znajomi", cmd_friends, true, false},
    {"wpis", cmd_post, true, true},
    {"tablica", cmd_feed, true, false},
    {"pokaz", cmd_show, true, false},
    {"lubie", cmd_like, true, true},
    {"komentuj", cmd_comment, true, true},
};

static const Command *find_command(const char *name) {
    for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
        if (strcmp(COMMANDS[i].name, name) == 0) {
            return &COMMANDS[i];
        }
    }
    return NULL;
}

static void print_prompt(Cli *cli) {
    if (cli->logged_in) {
        User me;
        fprintf(cli->out, "%s@ziomki> ", name_of(cli, cli->me, &me));
    } else {
        fprintf(cli->out, "ziomki> ");
    }
    fflush(cli->out);
}

int cli_run(const CliConfig *config) {
    Cli cli;
    memset(&cli, 0, sizeof cli);
    cli.config = config;
    cli.out = config->out;

    fprintf(cli.out, "Ziomki: mini serwis społecznościowy. Wpisz pomoc.\n");
    char line[CLI_LINE_MAX];
    for (;;) {
        print_prompt(&cli);
        if (fgets(line, sizeof line, config->in) == NULL) {
            fprintf(cli.out, "\n");
            break;
        }
        size_t length = strlen(line);
        if (length == sizeof line - 1 && line[length - 1] != '\n') {
            int c;
            while ((c = fgetc(config->in)) != '\n' && c != EOF) {
            }
            fprintf(cli.out, "! Linia jest za długa.\n");
            continue;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (config->echo_input) {
            fprintf(cli.out, "%s\n", line);
        }

        char *cursor = line;
        const char *name = next_token(&cursor);
        if (name == NULL) {
            continue;
        }
        if (strcmp(name, "koniec") == 0 || strcmp(name, "exit") == 0) {
            break;
        }
        const Command *command = find_command(name);
        if (command == NULL) {
            fprintf(cli.out, "! Nie znam polecenia „%s”. Wpisz pomoc.\n", name);
            continue;
        }
        if (command->needs_login && !cli.logged_in) {
            fprintf(cli.out, "! Najpierw się zaloguj: login <nick> albo rejestracja <nick>.\n");
            continue;
        }
        Result result = command->run(&cli, cursor);
        if (!result_is_ok(result)) {
            fprintf(cli.out, "! %s\n", result.message);
            continue;
        }
        if (command->mutates && config->persist != NULL) {
            config->persist(config->persist_context);
        }
    }
    fprintf(cli.out, "Do zobaczenia.\n");
    return 0;
}
