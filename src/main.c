/* mortar — an in-memory key-value store with TTL, served over a
 * line-based REPL. Commands mirror the Redis dialect closely enough
 * to feel familiar: SET key value [EX seconds], GET, DEL, TTL,
 * STATS, PING, EXIT. Lines starting with '#' are comments, so a
 * command file doubles as a replayable script. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "mem.h"
#include "store.h"
#include "util.h"

#define MAX_TOKENS 8

typedef int (*command_fn)(store_t *s, int argc, char **argv);

typedef struct {
    const char *name;
    command_fn fn;
    const char *args;
    const char *help;
} command_t;

static int cmd_set(store_t *s, int argc, char **argv) {
    if (argc < 3 || argc == 4 || argc > 5) {
        printf("(error) ERR usage: SET key value [EX seconds]\n");
        return 0;
    }
    uint64_t ttl_ms = 0;
    if (argc == 5) {
        if (strcmp(argv[3], "EX") != 0) {
            printf("(error) ERR usage: SET key value [EX seconds]\n");
            return 0;
        }
        char *end = NULL;
        long sec = strtol(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0' || sec <= 0) {
            printf("(error) ERR invalid EX seconds '%s'\n", argv[4]);
            return 0;
        }
        ttl_ms = (uint64_t)sec * 1000u;
    }
    store_set(s, argv[1], argv[2], ttl_ms);
    puts("OK");
    return 0;
}

static int cmd_get(store_t *s, int argc, char **argv) {
    if (argc != 2) {
        printf("(error) ERR usage: GET key\n");
        return 0;
    }
    const char *v = store_get(s, argv[1]);
    if (v == NULL) {
        puts("(nil)");
    } else {
        printf("%s\n", v);
    }
    return 0;
}

static int cmd_del(store_t *s, int argc, char **argv) {
    if (argc != 2) {
        printf("(error) ERR usage: DEL key\n");
        return 0;
    }
    printf("(integer) %d\n", store_del(s, argv[1]));
    return 0;
}

static int cmd_ttl(store_t *s, int argc, char **argv) {
    if (argc != 2) {
        printf("(error) ERR usage: TTL key\n");
        return 0;
    }
    int64_t ttl = store_ttl(s, argv[1]);
    printf("(integer) %lld\n", (long long)ttl);
    return 0;
}

static int cmd_ping(store_t *s, int argc, char **argv) {
    (void)s;
    (void)argc;
    (void)argv;
    puts("PONG");
    return 0;
}

static int cmd_stats(store_t *s, int argc, char **argv) {
    (void)argc;
    (void)argv;

    mem_class_stat_t cls[MEM_CLASSES];
    mem_totals_t totals;
    mem_stats(cls, &totals);

    size_t entries = htab_size(&s->table);
    size_t capacity = htab_capacity(&s->table);
    double load = capacity > 0 ? 100.0 * (double)entries / (double)capacity : 0.0;
    double hit_rate = s->gets > 0 ? 100.0 * (double)s->hits / (double)s->gets : 0.0;

    printf("entries        %zu / %zu slots (load %.1f%%)\n", entries,
           capacity, load);
    printf("tombstones     %zu   rehashes %zu\n", htab_tombstones(&s->table),
           htab_rehashes(&s->table));
    printf("key arena      %zu block(s), %zu bytes\n",
           htab_key_arena_blocks(&s->table), htab_key_arena_bytes(&s->table));
    for (int i = 0; i < MEM_CLASSES; i++) {
        printf("slab %5zu B   %zu/%zu live, high-water %zu, fallbacks %zu\n",
               cls[i].block_size, cls[i].used, cls[i].capacity,
               cls[i].high_water, cls[i].fallbacks);
    }
    printf("pooled live    %zu   heap fallback live %zu\n", totals.pooled,
           totals.heap);
    printf("commands       %llu set, %llu get, %llu del\n",
           (unsigned long long)s->sets, (unsigned long long)s->gets,
           (unsigned long long)s->deletes);
    printf("reads          %llu hit, %llu miss (%.1f%% hit rate)\n",
           (unsigned long long)s->hits, (unsigned long long)s->misses,
           hit_rate);
    printf("uptime         %llum%llds\n",
           (long long)(store_uptime_ms(s) / 60000),
           (long long)((store_uptime_ms(s) % 60000) / 1000));
    return 0;
}

static int cmd_exit(store_t *s, int argc, char **argv) {
    (void)s;
    (void)argc;
    (void)argv;
    return 1;
}

static const command_t commands[] = {
    {"SET", cmd_set, "key value [EX seconds]", "store a value, optionally expiring"},
    {"GET", cmd_get, "key", "fetch a value"},
    {"DEL", cmd_del, "key", "remove a key"},
    {"TTL", cmd_ttl, "key", "seconds left (-1 none, -2 missing)"},
    {"STATS", cmd_stats, "", "allocator and table diagnostics"},
    {"PING", cmd_ping, "", "liveness check"},
    {"EXIT", cmd_exit, "", "leave the shell"},
    {"QUIT", cmd_exit, "", "alias for EXIT"},
};

static const size_t command_count = sizeof(commands) / sizeof(commands[0]);

/* Splits `line` in place. Tokens are NUL-terminated; wrapping a token
 * in double quotes embeds separators ("two words"). Returns the token
 * count, or -1 on an unterminated quote. */
static int tokenize(char *line, char **out, int max_tokens) {
    int n = 0;
    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (n == max_tokens) {
            return -1;
        }
        out[n++] = p;
        if (*p == '"') {
            p++;
            char *dst = p - 1; /* shift left over the opening quote */
            while (*p != '\0' && *p != '"') {
                *dst++ = *p++;
            }
            if (*p != '"') {
                return -1;
            }
            *dst = '\0';
            p++; /* step past the closing quote */
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' &&
                   *p != '\n') {
                p++;
            }
            if (*p != '\0') {
                *p++ = '\0';
            }
        }
    }
    return n;
}

static void usage(FILE *out) {
    fprintf(out, "mortar — in-memory key-value store\n\n");
    for (size_t i = 0; i < command_count; i++) {
        fprintf(out, "  %-4s %-22s %s\n", commands[i].name, commands[i].args,
                commands[i].help);
    }
    fprintf(out, "\nCommands may be piped in or typed interactively; "
                 "'#' starts a comment.\n");
}

static int repl(FILE *in, store_t *s, bool prompt) {
    char *line = NULL;
    size_t cap = 0;
    char *argv[MAX_TOKENS];

    for (;;) {
        if (prompt) {
            printf("> ");
            fflush(stdout);
        }
        ssize_t len = getline(&line, &cap, in);
        if (len < 0) {
            break; /* EOF */
        }

        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }

        int argc = tokenize(line, argv, MAX_TOKENS);
        if (argc <= 0) {
            if (argc < 0) {
                printf("(error) ERR unterminated quote\n");
            }
            continue;
        }

        const command_t *cmd = NULL;
        for (size_t i = 0; i < command_count; i++) {
            if (strcasecmp(argv[0], commands[i].name) == 0) {
                cmd = &commands[i];
                break;
            }
        }
        if (cmd == NULL) {
            printf("(error) ERR unknown command '%s' — try STATS\n", argv[0]);
            continue;
        }
        if (cmd->fn(s, argc, argv) != 0) {
            break;
        }
    }

    free(line);
    return 0;
}

int main(int argc, char **argv) {
    FILE *in = stdin;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            in = fopen(argv[++i], "r");
            if (in == NULL) {
                fprintf(stderr, "mortar: cannot open script '%s'\n", argv[i]);
                return 1;
            }
            continue;
        }
        fprintf(stderr, "mortar: unknown argument '%s' (try --help)\n", argv[i]);
        return 1;
    }

    size_t sizes[MEM_CLASSES] = {64, 256, 4096};
    mem_init(sizes, 4096);

    store_t store;
    if (store_init(&store, NULL) != 0) {
        fprintf(stderr, "mortar: failed to initialize store\n");
        return 1;
    }

    int rc = repl(in, &store, in == stdin && isatty(fileno(stdin)));

    store_release(&store);
    mem_release();
    if (in != stdin) {
        fclose(in);
    }
    return rc;
}
