#ifndef NIX_SEARCH_UTIL_H
#define NIX_SEARCH_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define NS_VERSION "1.1.0"

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_reserve(StrBuf *sb, size_t extra);
void sb_append(StrBuf *sb, const char *s, size_t n);
void sb_puts(StrBuf *sb, const char *s);
void sb_putc(StrBuf *sb, char c);
void sb_printf(StrBuf *sb, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
char *sb_steal(StrBuf *sb);

char *read_file(const char *path, size_t max_size);
bool file_exists(const char *path);
char *trim(char *s);
bool starts_with(const char *s, const char *prefix);
bool ends_with(const char *s, const char *suffix);
char *str_lower(const char *s);
bool str_contains_ci(const char *haystack, const char *needle);
bool str_starts_with_ci(const char *s, const char *prefix);
const char *getenv_nonempty(const char *name);
const char *nix_bin(void);

/*
 * Run argv, capture stdout and stderr.  Both output pointers are always
 * allocated (possibly empty) on success.  Returns 0 if the child was run,
 * -1 on a system error (fork/pipe/exec failure is visible through the exit
 * code 127 and stderr).
 */
int run_capture(const char *const argv[], char **out, char **err,
                int *exit_code);

#endif
