#ifndef NIX_SEARCH_PACKAGE_H
#define NIX_SEARCH_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "channel.h"

typedef struct {
    char *name;
    char *full_name;
    char *version;
    char *description;
    char **build_inputs;
    size_t build_inputs_len;
    char **native_inputs;
    size_t native_inputs_len;
    char **propagated_inputs;
    size_t propagated_inputs_len;
} PackageInfo;

typedef enum {
    PACKAGE_FOUND,
    PACKAGE_NOT_FOUND,
    PACKAGE_ERROR
} PackageResult;

void package_info_init(PackageInfo *info);
void package_info_free(PackageInfo *info);

/*
 * Package names are attribute paths such as "hello" or
 * "python3Packages.requests".  Dots separate attributes.  Validation
 * rejects empty parts, control characters and absurdly long names.
 */
bool package_name_valid(const char *name, char **error);

/*
 * Look up package metadata in one nix eval call.  The package name is
 * passed through the environment, not interpolated into a Nix expression.
 */
PackageResult package_lookup(const Channel *channel, const char *system,
                             const char *name, PackageInfo *info,
                             char **error);

#endif
