#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void *xmalloc(size_t n)
{
    void *p = malloc(n == 0 ? 1 : n);
    if (p == NULL) {
        fputs("nix-search: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n == 0 ? 1 : n, size == 0 ? 1 : size);
    if (p == NULL) {
        fputs("nix-search: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n == 0 ? 1 : n);
    if (q == NULL) {
        fputs("nix-search: out of memory\n", stderr);
        exit(2);
    }
    return q;
}

char *xstrdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void sb_init(StrBuf *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb_init(sb);
}

void sb_reserve(StrBuf *sb, size_t extra)
{
    if (extra > SIZE_MAX - sb->len - 1) {
        fputs("nix-search: string is too large\n", stderr);
        exit(2);
    }
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return;
    }
    size_t cap = sb->cap == 0 ? 64 : sb->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
    if (sb->len == 0) {
        sb->data[0] = '\0';
    }
}

void sb_append(StrBuf *sb, const char *s, size_t n)
{
    if (n == 0) {
        return;
    }
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s)
{
    if (s != NULL) {
        sb_append(sb, s, strlen(s));
    }
}

void sb_putc(StrBuf *sb, char c)
{
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_printf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    sb->len += (size_t)n;
    va_end(ap2);
}

char *sb_steal(StrBuf *sb)
{
    char *p = sb->data != NULL ? sb->data : xstrdup("");
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return p;
}

char *read_file(const char *path, size_t max_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    StrBuf sb;
    sb_init(&sb);
    char buf[4096];
    while (!feof(f)) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0) {
            if (max_size > 0 && n > max_size - sb.len) {
                fclose(f);
                sb_free(&sb);
                return NULL;
            }
            sb_append(&sb, buf, n);
        }
        if (ferror(f)) {
            fclose(f);
            sb_free(&sb);
            return NULL;
        }
    }
    fclose(f);
    return sb_steal(&sb);
}

bool file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool ends_with(const char *s, const char *suffix)
{
    size_t slen = strlen(s);
    size_t suflen = strlen(suffix);
    return slen >= suflen && strcmp(s + slen - suflen, suffix) == 0;
}

char *str_lower(const char *s)
{
    size_t n = strlen(s);
    char *out = xmalloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[n] = '\0';
    return out;
}

bool str_contains_ci(const char *haystack, const char *needle)
{
    if (needle[0] == '\0') {
        return true;
    }
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] != '\0' &&
               tolower((unsigned char)p[i]) ==
                   tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

bool str_starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (*s == '\0' ||
            tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

const char *getenv_nonempty(const char *name)
{
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : NULL;
}

const char *nix_bin(void)
{
    const char *v = getenv_nonempty("NIX_SEARCH_NIX");
    return v != NULL ? v : "nix";
}

static void close_if_open(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

int run_capture(const char *const argv[], char **out, char **err,
                int *exit_code)
{
    int outpipe[2] = {-1, -1};
    int errpipe[2] = {-1, -1};
    pid_t pid;

    if (out == NULL || err == NULL || exit_code == NULL || argv == NULL ||
        argv[0] == NULL) {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    *err = NULL;
    *exit_code = -1;

    if (pipe(outpipe) < 0) {
        goto fail;
    }
    if (pipe(errpipe) < 0) {
        goto fail;
    }

    pid = fork();
    if (pid < 0) {
        goto fail;
    }
    if (pid == 0) {
        close(outpipe[0]);
        close(errpipe[0]);
        if (dup2(outpipe[1], STDOUT_FILENO) < 0 ||
            dup2(errpipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(outpipe[1]);
        close(errpipe[1]);
        execvp(argv[0], (char *const *)argv);
        fprintf(stderr, "nix-search: cannot execute %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    close(outpipe[1]);
    outpipe[1] = -1;
    close(errpipe[1]);
    errpipe[1] = -1;

    StrBuf ob;
    StrBuf eb;
    sb_init(&ob);
    sb_init(&eb);

    struct pollfd fds[2];
    fds[0].fd = outpipe[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = errpipe[0];
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    while (fds[0].fd >= 0 || fds[1].fd >= 0) {
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0) {
                continue;
            }
            if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) ==
                0) {
                continue;
            }
            char buf[8192];
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));
            if (n > 0) {
                if (i == 0) {
                    sb_append(&ob, buf, (size_t)n);
                } else {
                    sb_append(&eb, buf, (size_t)n);
                }
            } else if (n == 0) {
                close_if_open(&fds[i].fd);
            } else if (errno != EINTR && errno != EAGAIN) {
                close_if_open(&fds[i].fd);
            }
        }
    }

    close_if_open(&fds[0].fd);
    close_if_open(&fds[1].fd);
    close_if_open(&outpipe[0]);
    close_if_open(&errpipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            sb_free(&ob);
            sb_free(&eb);
            return -1;
        }
    }

    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = -1;
    }

    *out = sb_steal(&ob);
    *err = sb_steal(&eb);
    return 0;

fail:
    close_if_open(&outpipe[0]);
    close_if_open(&outpipe[1]);
    close_if_open(&errpipe[0]);
    close_if_open(&errpipe[1]);
    return -1;
}
