#include <os.h>
#include <limits.h>

#define MAX_ARGS 256
#define MAX_PIPE 32

enum {
    N_COMMAND,
    N_LIST,
    N_AND,
    N_OR,
    N_PIPE,
    N_BACKGROUND,
    N_IF,
    N_WHILE,
    N_FOR,
    N_SUBSHELL,
    N_NOT
};
struct token {
    char *word;
    int kind;
};
struct redirection {
    int fd, operation, duplicate;
    char *word;
    struct redirection *next;
};
struct node {
    int kind;
    struct node *a, *b, *c;
    char **words;
    size_t count;
    struct redirection *redirects;
    char *variable;
};
struct parser {
    struct token *tokens;
    size_t count, position;
    int error;
};
struct job {
    pid_t group, last;
    char *name;
    int stopped;
};
static struct job jobs[64];
static int last_status, interactive, leave_shell, exit_code, loop_break, loop_continue, loop_depth;
static pid_t shell_group, last_background, execution_group;
static int positional_count;
static char **positional;
static volatile int interrupted;
static char *trap_commands[NSIG];
static volatile unsigned long long trapped;
static int run(struct node *, bool);
static int run_source(const char *);

static void on_signal(int signal) {
    if (signal == SIGINT)
        interrupted = 1;
    if (signal > 0 && signal < 65)
        trapped |= 1ull << (signal - 1);
}
static void fatal_memory(void) {
    fputs("sh: out of memory\n", stderr);
    exit(2);
}
static void *allocate(size_t n) {
    void *p = calloc(1, n);
    if (!p)
        fatal_memory();
    return p;
}
static char *copy(const char *s) {
    char *p = strdup(s);
    if (!p)
        fatal_memory();
    return p;
}
static void add_text(char **buffer, size_t *length, size_t *capacity, const char *text, size_t n) {
    if (n > 1024 * 1024 - *length) {
        fputs("sh: expansion is too large\n", stderr);
        return;
    }
    if (*length + n + 1 > *capacity) {
        size_t next = (*length + n + 1) * 2;
        char *p = realloc(*buffer, next);
        if (!p)
            fatal_memory();
        *buffer = p;
        *capacity = next;
    }
    memcpy(*buffer + *length, text, n);
    *length += n;
    (*buffer)[*length] = 0;
}
static void add_char(char **b, size_t *n, size_t *c, char x) {
    add_text(b, n, c, &x, 1);
}
static bool operator_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')' ||
           c == '\n';
}
static void token_add(struct parser *p, const char *text, size_t n, int kind) {
    struct token *next = realloc(p->tokens, (p->count + 1) * sizeof(*next));
    if (!next)
        fatal_memory();
    p->tokens = next;
    p->tokens[p->count++] = (struct token){strndup(text, n), kind};
    if (!p->tokens[p->count - 1].word)
        fatal_memory();
}
static void lex(struct parser *p, const char *source) {
    size_t i = 0;
    while (source[i]) {
        if (source[i] == ' ' || source[i] == '\t' || source[i] == '\r') {
            i++;
            continue;
        }
        if (source[i] == '#') {
            while (source[i] && source[i] != '\n')
                i++;
            continue;
        }
        size_t start = i;
        if (isdigit(source[i]) && (source[i + 1] == '>' || source[i + 1] == '<')) {
            i++;
            char op = source[i++];
            if (source[i] == op)
                i++;
            if (source[i] == '&') {
                i++;
                if (source[i] == '-')
                    i++;
                else
                    while (isdigit(source[i]))
                        i++;
            }
            token_add(p, source + start, i - start, 2);
            continue;
        }
        if (operator_char(source[i])) {
            char op = source[i++];
            if ((op == '|' || op == '&' || op == '>') && source[i] == op)
                i++;
            if ((op == '>' || op == '<') && source[i] == '&') {
                i++;
                if (source[i] == '-')
                    i++;
                else
                    while (isdigit(source[i]))
                        i++;
            }
            token_add(p, source + start, i - start, (op == '>' || op == '<') ? 2 : 1);
            continue;
        }
        char quote = 0;
        unsigned substitution = 0;
        while (source[i]) {
            char c = source[i];
            if (c == '\\' && quote != '\'') {
                if (source[i + 1])
                    i += 2;
                else {
                    p->error = 1;
                    break;
                }
                continue;
            }
            if (c == '\'' || c == '"') {
                if (!quote)
                    quote = c;
                else if (quote == c)
                    quote = 0;
                i++;
                continue;
            }
            if (quote != '\'' && c == '$' && source[i + 1] == '(') {
                substitution++;
                i += 2;
                continue;
            }
            if (substitution) {
                if (c == ')')
                    substitution--;
                else if (c == '(')
                    substitution++;
                i++;
                continue;
            }
            if (!quote && (isspace((unsigned char)c) || operator_char(c)))
                break;
            i++;
        }
        if (quote || substitution)
            p->error = 1;
        if (i == start) {
            p->error = 1;
            break;
        }
        token_add(p, source + start, i - start, 0);
    }
}
static struct token *peek(struct parser *p) {
    return p->position < p->count ? &p->tokens[p->position] : NULL;
}
static bool is(struct parser *p, const char *word) {
    struct token *t = peek(p);
    return t && !strcmp(t->word, word);
}
static bool take(struct parser *p, const char *word) {
    if (is(p, word)) {
        p->position++;
        return true;
    }
    return false;
}
static void expect(struct parser *p, const char *word) {
    if (!take(p, word)) {
        fprintf(stderr, "sh: expected '%s'\n", word);
        p->error = 1;
    }
}
static struct node *node_new(int kind) {
    struct node *n = allocate(sizeof(*n));
    n->kind = kind;
    return n;
}
static void word_add(struct node *n, const char *word) {
    char **next = realloc(n->words, (n->count + 2) * sizeof(*next));
    if (!next)
        fatal_memory();
    n->words = next;
    n->words[n->count++] = copy(word);
    n->words[n->count] = NULL;
}
static bool stopped(struct parser *p, const char *stops) {
    if (!peek(p))
        return true;
    if (!stops)
        return false;
    const char *s = stops;
    while (*s) {
        const char *end = strchr(s, ' ');
        size_t len = end ? (size_t)(end - s) : strlen(s);
        if (strlen(peek(p)->word) == len && !strncmp(peek(p)->word, s, len))
            return true;
        if (!end)
            break;
        s = end + 1;
    }
    return false;
}
static struct node *parse_list(struct parser *, const char *);
static struct node *parse_andor(struct parser *);
static void parse_redirect(struct parser *p, struct node *n) {
    struct token *t = peek(p);
    const char *s = t->word;
    struct redirection *r = allocate(sizeof(*r));
    r->duplicate = -2;
    if (isdigit(*s))
        r->fd = *s++ - '0';
    else
        r->fd = *s == '<' ? 0 : 1;
    char op = *s++;
    r->operation = op == '<' ? 0 : 1;
    if (*s == op) {
        r->operation = 2;
        s++;
    }
    p->position++;
    if (*s == '&') {
        s++;
        r->duplicate = *s == '-' ? -1 : atoi(s);
    } else {
        struct token *word = peek(p);
        if (!word || word->kind) {
            p->error = 1;
            free(r);
            return;
        }
        r->word = copy(word->word);
        p->position++;
    }
    struct redirection **link = &n->redirects;
    while (*link)
        link = &(*link)->next;
    *link = r;
}
static struct node *parse_compound(struct parser *p) {
    struct node *n;
    if (take(p, "!")) {
        n = node_new(N_NOT);
        n->a = parse_compound(p);
        return n;
    }
    if (take(p, "if") || take(p, "elif")) {
        n = node_new(N_IF);
        n->a = parse_list(p, "then");
        expect(p, "then");
        n->b = parse_list(p, "else elif fi");
        if (take(p, "else")) {
            n->c = parse_list(p, "fi");
            expect(p, "fi");
        } else if (is(p, "elif"))
            n->c = parse_compound(p);
        else
            expect(p, "fi");
    } else if (take(p, "while") || take(p, "until")) {
        bool until = !strcmp(p->tokens[p->position - 1].word, "until");
        n = node_new(N_WHILE);
        n->count = until;
        n->a = parse_list(p, "do");
        expect(p, "do");
        n->b = parse_list(p, "done");
        expect(p, "done");
    } else if (take(p, "for")) {
        n = node_new(N_FOR);
        struct token *variable = peek(p);
        if (!variable || variable->kind) {
            p->error = 1;
            return n;
        }
        n->variable = copy(variable->word);
        p->position++;
        if (take(p, "in")) {
            while (peek(p) && peek(p)->kind == 0) {
                word_add(n, peek(p)->word);
                p->position++;
            }
        } else
            word_add(n, "\"$@\"");
        if (!take(p, ";") && !take(p, "\n"))
            p->error = 1;
        while (take(p, "\n")) {
        }
        expect(p, "do");
        n->a = parse_list(p, "done");
        expect(p, "done");
    } else if (take(p, "(")) {
        n = node_new(N_SUBSHELL);
        n->a = parse_list(p, ")");
        expect(p, ")");
    } else {
        n = node_new(N_COMMAND);
        while (peek(p) && (peek(p)->kind == 0 || peek(p)->kind == 2)) {
            if (peek(p)->kind == 2)
                parse_redirect(p, n);
            else {
                word_add(n, peek(p)->word);
                p->position++;
            }
        }
        if (!n->count && !n->redirects)
            p->error = 1;
    }
    while (peek(p) && peek(p)->kind == 2)
        parse_redirect(p, n);
    return n;
}
static struct node *parse_pipeline(struct parser *p) {
    struct node *n = parse_compound(p);
    while (take(p, "|")) {
        while (take(p, "\n")) {
        }
        struct node *pipe = node_new(N_PIPE);
        pipe->a = n;
        pipe->b = parse_compound(p);
        n = pipe;
    }
    return n;
}
static struct node *parse_andor(struct parser *p) {
    struct node *n = parse_pipeline(p);
    while (is(p, "&&") || is(p, "||")) {
        bool and = take(p, "&&");
        if (!and)
            expect(p, "||");
        while (take(p, "\n")) {
        }
        struct node *next = node_new(and? N_AND : N_OR);
        next->a = n;
        next->b = parse_pipeline(p);
        n = next;
    }
    return n;
}
static struct node *parse_list(struct parser *p, const char *stops) {
    struct node *result = NULL;
    while (take(p, ";") || take(p, "\n")) {
    }
    while (!p->error && !stopped(p, stops)) {
        size_t before = p->position;
        struct node *n = parse_andor(p);
        bool background = take(p, "&");
        if (background) {
            struct node *wrapper = node_new(N_BACKGROUND);
            wrapper->a = n;
            n = wrapper;
        }
        if (!result)
            result = n;
        else {
            struct node *list = node_new(N_LIST);
            list->a = result;
            list->b = n;
            result = list;
        }
        if (!background && !take(p, ";") && !take(p, "\n")) {
            if (!stopped(p, stops))
                p->error = 1;
            break;
        }
        while (take(p, ";") || take(p, "\n")) {
        }
        if (p->position == before) {
            p->error = 1;
            break;
        }
    }
    return result;
}
static void node_free(struct node *n) {
    if (!n)
        return;
    node_free(n->a);
    node_free(n->b);
    node_free(n->c);
    for (size_t i = 0; i < n->count && n->words; i++)
        free(n->words[i]);
    free(n->words);
    free(n->variable);
    while (n->redirects) {
        struct redirection *r = n->redirects;
        n->redirects = r->next;
        free(r->word);
        free(r);
    }
    free(n);
}
static char *substitute(const char *source, size_t length) {
    char *script = strndup(source, length);
    if (!script)
        fatal_memory();
    int descriptors[2];
    if (pipe(descriptors) < 0) {
        perror("sh: pipe");
        free(script);
        return copy("");
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("sh: fork");
        close(descriptors[0]);
        close(descriptors[1]);
        free(script);
        return copy("");
    }
    if (!pid) {
        close(descriptors[0]);
        dup2(descriptors[1], 1);
        close(descriptors[1]);
        interactive = 0;
        int status = run_source(script);
        exit(status);
    }
    close(descriptors[1]);
    char *output = NULL;
    size_t used = 0, capacity = 0;
    char buffer[1024];
    ssize_t n;
    while ((n = read(descriptors[0], buffer, sizeof(buffer))) > 0)
        add_text(&output, &used, &capacity, buffer, n);
    close(descriptors[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    free(script);
    if (!output)
        output = copy("");
    while (used && output[used - 1] == '\n')
        output[--used] = 0;
    return output;
}
static char *expand(const char *word) {
    char *output = NULL;
    size_t used = 0, capacity = 0;
    char quote = 0;
    for (size_t i = 0; word[i]; i++) {
        char c = word[i];
        if (c == '\'' || c == '"') {
            if (!quote) {
                quote = c;
                continue;
            }
            if (quote == c) {
                quote = 0;
                continue;
            }
        }
        if (c == '\\' && quote != '\'') {
            if (word[i + 1]) {
                char next = word[++i];
                if (next != '\n')
                    add_char(&output, &used, &capacity, next);
            }
            continue;
        }
        if (c == '~' && !i && !quote && (word[1] == '/' || !word[1])) {
            const char *home = getenv("HOME");
            if (home)
                add_text(&output, &used, &capacity, home, strlen(home));
            continue;
        }
        if (c != '$' || quote == '\'') {
            add_char(&output, &used, &capacity, c);
            continue;
        }
        i++;
        if (!word[i]) {
            add_char(&output, &used, &capacity, '$');
            break;
        }
        char number[64], name[256];
        const char *value = NULL;
        if (word[i] == '(') {
            size_t begin = i + 1, end = begin;
            int depth = 1;
            char q = 0;
            for (; word[end]; end++) {
                char v = word[end];
                if (v == '\\' && word[end + 1]) {
                    end++;
                    continue;
                }
                if (v == '\'' || v == '"') {
                    if (!q)
                        q = v;
                    else if (q == v)
                        q = 0;
                    continue;
                }
                if (!q) {
                    if (v == '(')
                        depth++;
                    else if (v == ')' && !--depth)
                        break;
                }
            }
            char *result = substitute(word + begin, end - begin);
            add_text(&output, &used, &capacity, result, strlen(result));
            free(result);
            i = end;
            continue;
        }
        if (word[i] == '?') {
            snprintf(number, sizeof(number), "%d", last_status);
            value = number;
        } else if (word[i] == '$') {
            snprintf(number, sizeof(number), "%d", getpid());
            value = number;
        } else if (word[i] == '!') {
            snprintf(number, sizeof(number), "%d", last_background);
            value = number;
        } else if (word[i] == '#') {
            snprintf(number, sizeof(number), "%d", positional_count > 0 ? positional_count - 1 : 0);
            value = number;
        } else if (word[i] == '@' || word[i] == '*') {
            for (int j = 1; j < positional_count; j++) {
                if (j > 1)
                    add_char(&output, &used, &capacity, ' ');
                add_text(&output, &used, &capacity, positional[j], strlen(positional[j]));
            }
            continue;
        } else if (isdigit(word[i])) {
            int at = word[i] - '0';
            value = at < positional_count ? positional[at] : "";
        } else if (word[i] == '{') {
            size_t n = 0;
            i++;
            while (word[i] && word[i] != '}' && n < 255)
                name[n++] = word[i++];
            name[n] = 0;
            value = getenv(name);
        } else if (isalpha(word[i]) || word[i] == '_') {
            size_t n = 0;
            while ((isalnum(word[i]) || word[i] == '_') && n < 255)
                name[n++] = word[i++];
            name[n] = 0;
            i--;
            value = getenv(name);
        } else {
            add_char(&output, &used, &capacity, '$');
            i--;
            continue;
        }
        if (value)
            add_text(&output, &used, &capacity, value, strlen(value));
    }
    return output ? output : copy("");
}
static bool pattern(const char *p, const char *s) {
    while (*p) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return true;
            do {
                if (pattern(p, s))
                    return true;
            } while (*s++);
            return false;
        }
        if (*p == '?') {
            if (!*s)
                return false;
            p++;
            s++;
            continue;
        }
        if (*p == '[') {
            p++;
            bool invert = *p == '!' || *p == '^', match = false;
            if (invert)
                p++;
            while (*p && *p != ']') {
                char a = *p++, b = a;
                if (*p == '-' && p[1] && p[1] != ']') {
                    p++;
                    b = *p++;
                }
                if (*s >= a && *s <= b)
                    match = true;
            }
            if (*p == ']')
                p++;
            if (!*s || match == invert)
                return false;
            s++;
            continue;
        }
        if (*p++ != *s++)
            return false;
    }
    return !*s;
}
static void argument(char ***vector, size_t *count, const char *word) {
    if (*count >= MAX_ARGS - 1) {
        fputs("sh: too many arguments\n", stderr);
        return;
    }
    char **v = realloc(*vector, (*count + 2) * sizeof(*v));
    if (!v)
        fatal_memory();
    *vector = v;
    v[(*count)++] = copy(word);
    v[*count] = NULL;
}
static void glob_argument(char ***vector, size_t *count, const char *word, bool quoted) {
    if (quoted || !strpbrk(word, "*?[")) {
        argument(vector, count, word);
        return;
    }
    char *copy_word = copy(word), *slash = strrchr(copy_word, '/'),
         *mask = slash ? slash + 1 : copy_word;
    const char *directory = ".";
    if (slash) {
        *slash = 0;
        directory = copy_word[0] ? copy_word : "/";
    }
    DIR *d = opendir(directory);
    size_t before = *count;
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d))) {
            if (entry->d_name[0] == '.' && mask[0] != '.')
                continue;
            if (!pattern(mask, entry->d_name))
                continue;
            char path[PATH_MAX];
            if (slash)
                snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            else
                snprintf(path, sizeof(path), "%s", entry->d_name);
            argument(vector, count, path);
        }
        closedir(d);
    }
    if (before == *count)
        argument(vector, count, word);
    free(copy_word);
}
static char **expand_words(struct node *n, size_t *count) {
    char **words = NULL;
    *count = 0;
    for (size_t i = 0; i < n->count; i++) {
        const char *raw = n->words[i];
        if (!strcmp(raw, "\"$@\"")) {
            for (int j = 1; j < positional_count; j++)
                argument(&words, count, positional[j]);
            continue;
        }
        bool quoted =
            strchr(raw, '\'') || strchr(raw, '"') || strchr(raw, '\\') || strchr(raw, '=');
        char *word = expand(raw);
        if (quoted)
            glob_argument(&words, count, word, true);
        else {
            char *state = NULL;
            for (char *part = strtok_r(word, " \t\n", &state); part;
                 part = strtok_r(NULL, " \t\n", &state))
                glob_argument(&words, count, part, false);
        }
        free(word);
    }
    if (!words)
        words = allocate(sizeof(char *));
    return words;
}
static void arguments_free(char **v) {
    if (v) {
        for (size_t i = 0; v[i]; i++)
            free(v[i]);
        free(v);
    }
}
static int redirect(struct redirection *r) {
    for (; r; r = r->next) {
        if (r->duplicate >= -1) {
            if (r->duplicate == -1) {
                close(r->fd);
                continue;
            }
            if (dup2(r->duplicate, r->fd) < 0) {
                perror("sh: dup2");
                return -1;
            }
            continue;
        }
        char *path = expand(r->word);
        int flags = r->operation == 0
                        ? O_RDONLY
                        : O_WRONLY | O_CREAT | (r->operation == 2 ? O_APPEND : O_TRUNC);
        int fd = open(path, flags, 0666);
        if (fd < 0) {
            perror(path);
            free(path);
            return -1;
        }
        free(path);
        if (fd != r->fd) {
            if (dup2(fd, r->fd) < 0) {
                perror("sh: redirect");
                close(fd);
                return -1;
            }
            close(fd);
        }
    }
    return 0;
}
static void reap_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        for (unsigned i = 0; i < 64; i++)
            if (jobs[i].last == pid) {
                if (WIFSTOPPED(status))
                    jobs[i].stopped = 1;
                else {
                    if (interactive)
                        fprintf(stderr, "[%u] Done %s\n", i + 1, jobs[i].name);
                    free(jobs[i].name);
                    jobs[i] = (struct job){0};
                }
            }
    }
}
static void store_job(pid_t group, pid_t last, bool stopped_job) {
    for (unsigned i = 0; i < 64; i++)
        if (!jobs[i].group) {
            jobs[i] = (struct job){group, last, copy("job"), stopped_job};
            if (interactive)
                fprintf(stderr, "[%u] %d%s\n", i + 1, group, stopped_job ? " Stopped" : "");
            return;
        }
    fputs("sh: job table full\n", stderr);
}
static int wait_group(pid_t group, pid_t last, size_t children) {
    int result = 0;
    while (children) {
        int status;
        pid_t pid = waitpid(-group, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        children--;
        if (WIFSTOPPED(status)) {
            store_job(group, last, true);
            result = 128 + WSTOPSIG(status);
            break;
        }
        if (pid == last)
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    if (interactive)
        tcsetpgrp(0, shell_group);
    return result;
}
static int builtin(char **v, size_t count, bool *handled) {
    *handled = true;
    if (!count)
        return 0;
    if (!strcmp(v[0], "cd")) {
        const char *path = count > 1 ? v[1] : getenv("HOME");
        if (!path)
            path = "/";
        char *old = getcwd(NULL, 0);
        if (chdir(path) < 0) {
            perror("cd");
            free(old);
            return 1;
        }
        if (old) {
            setenv("OLDPWD", old, 1);
            free(old);
        }
        char *cwd = getcwd(NULL, 0);
        if (cwd) {
            setenv("PWD", cwd, 1);
            free(cwd);
        }
        return 0;
    }
    if (!strcmp(v[0], "exit")) {
        exit_code = count > 1 ? atoi(v[1]) : last_status;
        leave_shell = 1;
        return exit_code;
    }
    if (!strcmp(v[0], "export")) {
        if (count == 1) {
            for (char **p = environ; p && *p; p++)
                puts(*p);
            return 0;
        }
        for (size_t i = 1; i < count; i++) {
            char *equal = strchr(v[i], '=');
            if (equal) {
                *equal = 0;
                if (setenv(v[i], equal + 1, 1) < 0) {
                    perror("export");
                    return 1;
                }
            } else if (!getenv(v[i]))
                setenv(v[i], "", 1);
        }
        return 0;
    }
    if (!strcmp(v[0], "unset")) {
        for (size_t i = 1; i < count; i++)
            if (unsetenv(v[i]) < 0)
                return 1;
        return 0;
    }
    if (!strcmp(v[0], ":"))
        return 0;
    if (!strcmp(v[0], "exec")) {
        if (count < 2)
            return 0;
        execvp(v[1], v + 1);
        perror(v[1]);
        return errno == ENOENT ? 127 : 126;
    }
    if (!strcmp(v[0], "set")) {
        if (count > 1 && !strcmp(v[1], "--")) {
            positional_count = count - 1;
            char **next = allocate((positional_count + 1) * sizeof(char *));
            next[0] = copy(positional_count ? positional[0] : "sh");
            for (int i = 1; i < positional_count; i++)
                next[i] = copy(v[i + 1]);
            positional = next;
        } else
            for (char **p = environ; p && *p; p++)
                puts(*p);
        return 0;
    }
    if (!strcmp(v[0], "shift")) {
        int n = count > 1 ? atoi(v[1]) : 1;
        if (n < 0 || n >= positional_count)
            return 1;
        for (int i = 1; i + n < positional_count; i++)
            positional[i] = positional[i + n];
        positional_count -= n;
        return 0;
    }
    if (!strcmp(v[0], "read")) {
        char *line = NULL;
        size_t capacity = 0;
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0) {
            free(line);
            return 1;
        }
        if (length && line[length - 1] == '\n')
            line[length - 1] = 0;
        char *rest = line;
        for (size_t i = 1; i < count; i++) {
            while (*rest == ' ' || *rest == '\t')
                rest++;
            if (i + 1 == count) {
                setenv(v[i], rest, 1);
                break;
            }
            char *end = rest + strcspn(rest, " \t");
            char saved = *end;
            *end = 0;
            setenv(v[i], rest, 1);
            *end = saved;
            rest = saved ? end + 1 : end;
        }
        free(line);
        return 0;
    }
    if (!strcmp(v[0], "umask")) {
        if (count > 1)
            umask(strtoul(v[1], NULL, 8));
        else {
            mode_t mask = umask(0);
            umask(mask);
            printf("%04o\n", mask);
        }
        return 0;
    }
    if (!strcmp(v[0], "wait")) {
        int status = 0;
        pid_t result;
        if (count > 1) {
            do {
                result = waitpid(atoi(v[1]), &status, 0);
            } while (result < 0 && errno == EINTR);
        } else {
            do {
                result = waitpid(-1, &status, 0);
            } while (result > 0 || (result < 0 && errno == EINTR));
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    if (!strcmp(v[0], "jobs")) {
        reap_jobs();
        for (unsigned i = 0; i < 64; i++)
            if (jobs[i].group)
                printf("[%u] %s %d\n", i + 1, jobs[i].stopped ? "Stopped" : "Running",
                       jobs[i].group);
        return 0;
    }
    if (!strcmp(v[0], "fg") || !strcmp(v[0], "bg")) {
        int index = count > 1 ? atoi(v[1] + (v[1][0] == '%')) - 1 : 0;
        if (index < 0 || index >= 64 || !jobs[index].group) {
            fputs("sh: no such job\n", stderr);
            return 1;
        }
        pid_t group = jobs[index].group, last = jobs[index].last;
        kill(-group, SIGCONT);
        jobs[index].stopped = 0;
        if (v[0][0] == 'f') {
            if (interactive)
                tcsetpgrp(0, group);
            free(jobs[index].name);
            jobs[index] = (struct job){0};
            return wait_group(group, last, 1);
        }
        return 0;
    }
    if (!strcmp(v[0], "break") || !strcmp(v[0], "continue")) {
        if (!loop_depth) {
            fputs("sh: not in a loop\n", stderr);
            return 1;
        }
        int n = count > 1 ? atoi(v[1]) : 1;
        if (n < 1)
            return 1;
        if (n > loop_depth)
            n = loop_depth;
        if (v[0][0] == 'b')
            loop_break = n;
        else
            loop_continue = n;
        return 0;
    }
    if (!strcmp(v[0], ".") || !strcmp(v[0], "source")) {
        if (count < 2)
            return 2;
        FILE *file = fopen(v[1], "r");
        if (!file) {
            perror(v[1]);
            return 1;
        }
        char *script = NULL;
        size_t n = 0, cap = 0;
        char b[4096];
        size_t got;
        while ((got = fread(b, 1, sizeof(b), file)))
            add_text(&script, &n, &cap, b, got);
        fclose(file);
        int status = run_source(script ? script : "");
        free(script);
        return status;
    }
    if (!strcmp(v[0], "trap")) {
        if (count < 2) {
            for (int i = 1; i < NSIG; i++)
                if (trap_commands[i])
                    printf("trap '%s' %d\n", trap_commands[i], i);
            return 0;
        }
        for (size_t i = 2; i < count; i++) {
            int signal_number = atoi(v[i]);
            if (signal_number < 1 || signal_number >= NSIG || signal_number == SIGKILL ||
                signal_number == SIGSTOP)
                return 1;
            free(trap_commands[signal_number]);
            trap_commands[signal_number] = !strcmp(v[1], "-") ? NULL : copy(v[1]);
            signal(signal_number, trap_commands[signal_number]
                                      ? (*trap_commands[signal_number] ? on_signal : SIG_IGN)
                                      : SIG_DFL);
        }
        return 0;
    }
    *handled = false;
    return 0;
}
static bool assignment(const char *s) {
    const char *equal = strchr(s, '=');
    if (!equal || equal == s || (!isalpha(*s) && *s != '_'))
        return false;
    for (const char *p = s; p < equal; p++)
        if (!isalnum(*p) && *p != '_')
            return false;
    return true;
}
static void child_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}
static int command(struct node *n, bool child) {
    size_t count;
    char **words = expand_words(n, &count);
    size_t first = 0;
    while (first < count && assignment(words[first])) {
        char *equal = strchr(words[first], '=');
        *equal = 0;
        setenv(words[first], equal + 1, 1);
        *equal = '=';
        first++;
    }
    bool handled = false;
    int saved[128];
    for (unsigned i = 0; i < 128; i++)
        saved[i] = -2;
    if (!child)
        for (struct redirection *r = n->redirects; r; r = r->next)
            if (r->fd >= 0 && r->fd < 128 && saved[r->fd] == -2) {
                saved[r->fd] = dup(r->fd);
            }
    fflush(NULL);
    if (redirect(n->redirects) < 0) {
        arguments_free(words);
        return 1;
    }
    int status = builtin(words + first, count - first, &handled);
    if (!handled && count > first) {
        if (child) {
            execvp(words[first], words + first);
            if (errno == ENOEXEC) {
                char **args = allocate((count - first + 2) * sizeof(char *));
                args[0] = "sh";
                for (size_t i = first; i < count; i++)
                    args[i - first + 1] = words[i];
                execv("/bin/sh", args);
            }
            perror(words[first]);
            status = errno == ENOENT ? 127 : 126;
        } else {
            pid_t pid = fork();
            if (!pid) {
                child_signals();
                pid_t group = execution_group ? execution_group : getpid();
                setpgid(0, group);
                if (interactive)
                    tcsetpgrp(0, group);
                execvp(words[first], words + first);
                if (errno == ENOEXEC) {
                    char **args = allocate((count - first + 2) * sizeof(char *));
                    args[0] = "sh";
                    for (size_t i = first; i < count; i++)
                        args[i - first + 1] = words[i];
                    execv("/bin/sh", args);
                }
                perror(words[first]);
                _exit(errno == ENOENT ? 127 : 126);
            }
            if (pid < 0) {
                perror("sh: fork");
                status = 1;
            } else {
                pid_t group = execution_group ? execution_group : pid;
                setpgid(pid, group);
                if (interactive)
                    tcsetpgrp(0, group);
                status = wait_group(group, pid, 1);
            }
        }
    }
    fflush(NULL);
    if (!child)
        for (unsigned i = 0; i < 128; i++)
            if (saved[i] != -2) {
                if (saved[i] >= 0) {
                    dup2(saved[i], i);
                    close(saved[i]);
                } else
                    close(i);
            }
    arguments_free(words);
    return status;
}
static void flatten(struct node *n, struct node **stages, size_t *count) {
    if (n->kind == N_PIPE) {
        flatten(n->a, stages, count);
        flatten(n->b, stages, count);
    } else if (*count < MAX_PIPE)
        stages[(*count)++] = n;
}
static int pipeline(struct node *n, bool background) {
    struct node *stages[MAX_PIPE];
    size_t count = 0;
    flatten(n, stages, &count);
    int previous = -1;
    pid_t group = execution_group, last = 0;
    size_t spawned = 0;
    fflush(NULL);
    for (size_t i = 0; i < count; i++) {
        int descriptors[2] = {-1, -1};
        if (i + 1 < count && pipe(descriptors) < 0) {
            perror("sh: pipe");
            break;
        }
        pid_t pid = fork();
        if (pid == 0) {
            child_signals();
            pid_t target = group ? group : getpid();
            setpgid(0, target);
            if (interactive && !background)
                tcsetpgrp(0, target);
            if (previous >= 0) {
                dup2(previous, 0);
                close(previous);
            }
            if (descriptors[1] >= 0) {
                dup2(descriptors[1], 1);
                close(descriptors[0]);
                close(descriptors[1]);
            }
            interactive = 0;
            execution_group = target;
            int result = run(stages[i], stages[i]->kind == N_COMMAND);
            exit(result);
        }
        if (previous >= 0)
            close(previous);
        if (descriptors[1] >= 0)
            close(descriptors[1]);
        previous = descriptors[0];
        if (pid < 0) {
            perror("sh: fork");
            break;
        }
        if (!group)
            group = pid;
        setpgid(pid, group);
        last = pid;
        spawned++;
    }
    if (previous >= 0)
        close(previous);
    if (!spawned)
        return 1;
    if (background) {
        last_background = last;
        store_job(group, last, false);
        return 0;
    }
    if (interactive)
        tcsetpgrp(0, group);
    return wait_group(group, last, spawned);
}
static int run(struct node *n, bool child) {
    if (!n || leave_shell || loop_break || loop_continue)
        return last_status;
    int result = 0;
    switch (n->kind) {
    case N_COMMAND:
        result = command(n, child);
        break;
    case N_LIST:
        run(n->a, false);
        result = run(n->b, false);
        break;
    case N_AND:
        result = run(n->a, false);
        if (!result)
            result = run(n->b, false);
        break;
    case N_OR:
        result = run(n->a, false);
        if (result)
            result = run(n->b, false);
        break;
    case N_PIPE:
        result = pipeline(n, false);
        break;
    case N_BACKGROUND:
        result = pipeline(n->a, true);
        break;
    case N_SUBSHELL: {
        fflush(NULL);
        pid_t pid = fork();
        if (!pid) {
            interactive = 0;
            int status = run(n->a, false);
            exit(status);
        }
        if (pid < 0) {
            perror("sh: fork");
            result = 1;
        } else {
            int status;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        break;
    }
    case N_NOT:
        result = !run(n->a, false);
        break;
    case N_IF:
        result = run(n->a, false);
        result = run(result ? n->c : n->b, false);
        break;
    case N_WHILE:
        loop_depth++;
        while (!leave_shell) {
            int condition = run(n->a, false);
            if ((condition == 0) == (n->count != 0))
                break;
            result = run(n->b, false);
            if (loop_break) {
                loop_break--;
                break;
            }
            if (loop_continue) {
                loop_continue--;
                if (loop_continue)
                    break;
            }
        }
        loop_depth--;
        break;
    case N_FOR: {
        size_t count;
        char **words = expand_words(n, &count);
        loop_depth++;
        for (size_t i = 0; i < count && !leave_shell; i++) {
            setenv(n->variable, words[i], 1);
            result = run(n->a, false);
            if (loop_break) {
                loop_break--;
                break;
            }
            if (loop_continue) {
                loop_continue--;
                if (loop_continue)
                    break;
            }
        }
        loop_depth--;
        arguments_free(words);
        break;
    }
    }
    last_status = result;
    return result;
}
static int run_source(const char *text) {
    struct parser parser = {0};
    lex(&parser, text);
    struct node *root = parse_list(&parser, NULL);
    int status;
    if (parser.error) {
        fputs("sh: syntax error\n", stderr);
        status = 2;
    } else
        status = run(root, false);
    node_free(root);
    for (size_t i = 0; i < parser.count; i++)
        free(parser.tokens[i].word);
    free(parser.tokens);
    return status;
}
static bool complete_source(const char *s) {
    char quote = 0;
    int parentheses = 0, blocks = 0;
    bool start = true;
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '\\' && quote != '\'') {
            if (s[i + 1])
                i++;
            else
                return false;
            continue;
        }
        if (s[i] == '\'' || s[i] == '"') {
            if (!quote)
                quote = s[i];
            else if (quote == s[i])
                quote = 0;
            continue;
        }
        if (quote)
            continue;
        if (s[i] == '(') {
            parentheses++;
            start = true;
            continue;
        }
        if (s[i] == ')') {
            parentheses--;
            continue;
        }
        if (s[i] == ';' || s[i] == '\n' || s[i] == '|' || s[i] == '&') {
            start = true;
            continue;
        }
        if (isspace((unsigned char)s[i]))
            continue;
        size_t n = 0;
        while (s[i + n] && !isspace((unsigned char)s[i + n]) && !operator_char(s[i + n]))
            n++;
        if (!n) {
            start = false;
            continue;
        }
        if (start) {
            if ((n == 2 && !strncmp(s + i, "if", n)) || (n == 5 && !strncmp(s + i, "while", n)) ||
                (n == 5 && !strncmp(s + i, "until", n))) {
                blocks++;
                start = true;
            } else if (n == 3 && !strncmp(s + i, "for", n)) {
                blocks++;
                start = false;
            } else if ((n == 2 && !strncmp(s + i, "fi", n)) ||
                       (n == 4 && !strncmp(s + i, "done", n))) {
                blocks--;
                start = false;
            } else if ((n == 4 && !strncmp(s + i, "then", n)) ||
                       (n == 2 && !strncmp(s + i, "do", n)) ||
                       (n == 4 && !strncmp(s + i, "else", n)) ||
                       (n == 4 && !strncmp(s + i, "elif", n)))
                start = true;
            else
                start = false;
        }
        i += n - 1;
    }
    return !quote && parentheses <= 0 && blocks <= 0;
}
int main(int argc, char **argv) {
    shell_group = getpgrp();
    positional = argv;
    positional_count = argc;
    if (argc > 1 && !strcmp(argv[1], "-c")) {
        if (argc < 3)
            return 2;
        positional = argv + 2;
        positional_count = argc - 2;
        return run_source(argv[2]);
    }
    if (argc > 1) {
        FILE *file = fopen(argv[1], "r");
        if (!file) {
            perror(argv[1]);
            return 1;
        }
        positional = argv + 1;
        positional_count = argc - 1;
        char *source = NULL;
        size_t size = 0, capacity = 0;
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), file)))
            add_text(&source, &size, &capacity, b, n);
        fclose(file);
        int status = run_source(source ? source : "");
        free(source);
        return leave_shell ? exit_code : status;
    }
    interactive = isatty(0);
    if (interactive) {
        struct sigaction interrupt_action = {.sa_handler = on_signal};
        sigaction(SIGINT, &interrupt_action, NULL);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        tcsetpgrp(0, shell_group);
        puts("Aster shell - type 'help' for commands");
    }
    char *line = NULL, *source = NULL;
    size_t line_capacity = 0, size = 0, capacity = 0;
    while (!leave_shell) {
        reap_jobs();
        for (unsigned i = 1; i < NSIG; i++)
            if ((trapped & (1ull << (i - 1))) && trap_commands[i]) {
                trapped &= ~(1ull << (i - 1));
                run_source(trap_commands[i]);
            }
        if (interactive) {
            fputs(size ? "> " : "aster$ ", stdout);
            fflush(stdout);
        }
        errno = 0;
        ssize_t n = getline(&line, &line_capacity, stdin);
        if (n < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                interrupted = 0;
                size = 0;
                continue;
            }
            break;
        }
        add_text(&source, &size, &capacity, line, n);
        if (!complete_source(source))
            continue;
        run_source(source);
        size = 0;
        if (source)
            source[0] = 0;
    }
    free(line);
    free(source);
    return leave_shell ? exit_code : last_status;
}
