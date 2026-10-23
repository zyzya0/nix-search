#ifndef NIX_SEARCH_CHANNEL_H
#define NIX_SEARCH_CHANNEL_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    char *ref;        /* flake ref passed to nix search */
    char *branch;     /* nixos-26.11, nixos-unstable, ... */
    char *version;    /* 26.11, unstable, ... */
    char *source;     /* human readable detection source */
    char *local_path; /* local nixpkgs checkout, if any */
    bool local;
} Channel;

void channel_init(Channel *ch);
void channel_free(Channel *ch);

/*
 * Detect the channel or use an explicit override.
 * override == NULL or "auto" enables auto-detection.
 * Returns 0 on success, -1 on invalid explicit override.
 */
int channel_detect(Channel *ch, const char *override, bool verbose,
                   char **error);

/* Use an explicit flake ref or local path. */
int channel_set_flake(Channel *ch, const char *ref);

/* Helpers, also used by the tests. */
char *channel_branch_from_version(const char *version);
char *channel_branch_from_name(const char *name);

#endif
