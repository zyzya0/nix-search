#include "channel.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void channel_clear(Channel *ch)
{
    free(ch->ref);
    free(ch->branch);
    free(ch->version);
    free(ch->source);
    free(ch->local_path);
    memset(ch, 0, sizeof(*ch));
}

void channel_init(Channel *ch)
{
    memset(ch, 0, sizeof(*ch));
}

void channel_free(Channel *ch)
{
    channel_clear(ch);
}

static bool parse_major_minor(const char *s, int *major, int *minor)
{
    while (*s != '\0' && !isdigit((unsigned char)*s)) {
        s++;
    }
    if (!isdigit((unsigned char)*s)) {
        return false;
    }

    char *end = NULL;
    long ma = strtol(s, &end, 10);
    if (end == s || *end != '.') {
        return false;
    }
    char *end2 = NULL;
    long mi = strtol(end + 1, &end2, 10);
    if (end2 == end + 1 || ma < 0 || ma > 999 || mi < 0 || mi > 99) {
        return false;
    }
    *major = (int)ma;
    *minor = (int)mi;
    return true;
}

char *channel_branch_from_version(const char *version)
{
    if (version == NULL) {
        return NULL;
    }

    int major;
    int minor;
    if (!parse_major_minor(version, &major, &minor)) {
        return NULL;
    }
    if (strstr(version, "pre") != NULL ||
        strstr(version, "unstable") != NULL) {
        return xstrdup("nixos-unstable");
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "nixos-%d.%02d", major, minor);
    return xstrdup(buf);
}

char *channel_branch_from_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    if (strstr(name, "unstable") != NULL || strstr(name, "master") != NULL) {
        return xstrdup("nixos-unstable");
    }

    int major;
    int minor;
    if (!parse_major_minor(name, &major, &minor)) {
        return NULL;
    }
    if (strstr(name, "pre") != NULL) {
        return xstrdup("nixos-unstable");
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "nixos-%d.%02d", major, minor);
    return xstrdup(buf);
}

static char *version_from_branch(const char *branch)
{
    if (branch == NULL || branch[0] == '\0') {
        return xstrdup("");
    }
    if (strstr(branch, "unstable") != NULL) {
        return xstrdup("unstable");
    }
    int major;
    int minor;
    if (parse_major_minor(branch, &major, &minor)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d.%02d", major, minor);
        return xstrdup(buf);
    }
    return xstrdup("");
}

static void set_branch(Channel *ch, const char *branch, const char *source)
{
    channel_clear(ch);
    ch->branch = xstrdup(branch);
    ch->ref = NULL;

    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "github:NixOS/nixpkgs/%s", branch);
    ch->ref = sb_steal(&sb);

    ch->version = version_from_branch(branch);
    ch->source = xstrdup(source);
    ch->local = false;
}

static void set_local(Channel *ch, const char *path, const char *source)
{
    char resolved[PATH_MAX];
    const char *use = path;
    if (realpath(path, resolved) != NULL) {
        use = resolved;
    }

    channel_clear(ch);
    ch->local = true;
    ch->local_path = xstrdup(use);
    ch->source = xstrdup(source);

    StrBuf file;
    sb_init(&file);
    sb_printf(&file, "%s/.version", use);
    char *raw = read_file(file.data, 4096);
    if (raw == NULL) {
        sb_free(&file);
        sb_init(&file);
        sb_printf(&file, "%s/lib/.version", use);
        raw = read_file(file.data, 4096);
    }
    sb_free(&file);

    if (raw != NULL) {
        char *t = trim(raw);
        ch->version = xstrdup(t);
        free(raw);
    } else {
        ch->version = xstrdup("");
    }

    char *branch = channel_branch_from_version(ch->version);
    if (branch == NULL) {
        branch = xstrdup("nixos-unstable");
    }
    ch->branch = branch;

    StrBuf flake;
    sb_init(&flake);
    sb_printf(&flake, "%s/flake.nix", use);
    bool has_flake = file_exists(flake.data);
    sb_free(&flake);

    StrBuf ref;
    sb_init(&ref);
    if (has_flake) {
        sb_printf(&ref, "path:%s", use);
    } else {
        sb_printf(&ref, "github:NixOS/nixpkgs/%s", branch);
    }
    ch->ref = sb_steal(&ref);
}

int channel_set_flake(Channel *ch, const char *ref)
{
    if (ref == NULL || ref[0] == '\0') {
        return -1;
    }

    if (starts_with(ref, "path:")) {
        set_local(ch, ref + strlen("path:"), "user-specified path");
        return 0;
    }
    if (ref[0] == '/') {
        set_local(ch, ref, "user-specified path");
        return 0;
    }

    channel_clear(ch);
    ch->ref = xstrdup(ref);
    ch->source = xstrdup("user-specified flake");
    ch->local = false;

    const char *prefix = "github:NixOS/nixpkgs/";
    if (starts_with(ref, prefix)) {
        const char *tail = ref + strlen(prefix);
        size_t len = strcspn(tail, "?#");
        char *branch = xstrndup(tail, len);
        if (branch[0] != '\0') {
            ch->branch = branch;
            ch->version = version_from_branch(branch);
        } else {
            free(branch);
            ch->branch = xstrdup("");
            ch->version = xstrdup("");
        }
    } else {
        ch->branch = xstrdup("");
        ch->version = xstrdup("");
    }
    ch->local_path = NULL;
    return 0;
}

static bool looks_like_flake(const char *s)
{
    return strchr(s, ':') != NULL || s[0] == '/' || s[0] == '.';
}

static bool set_branch_from_name(Channel *ch, const char *name,
                                 const char *source)
{
    char *branch = channel_branch_from_name(name);
    if (branch == NULL) {
        return false;
    }
    set_branch(ch, branch, source);
    free(branch);
    return true;
}

static bool detect_from_find_file(Channel *ch, bool verbose)
{
    const char *tool = getenv_nonempty("NIX_SEARCH_NIX_INSTANTIATE");
    if (tool == NULL) {
        tool = "nix-instantiate";
    }
    const char *argv[] = {tool, "--find-file", "nixpkgs", NULL};
    char *out = NULL;
    char *err = NULL;
    int code = -1;

    if (run_capture(argv, &out, &err, &code) != 0) {
        free(out);
        free(err);
        return false;
    }
    if (code != 0) {
        if (verbose && err[0] != '\0') {
            fprintf(stderr, "nix-search: nix-instantiate: %s", err);
        }
        free(out);
        free(err);
        return false;
    }

    char *path = trim(out);
    bool ok = path[0] == '/' && file_exists(path);
    if (ok) {
        set_local(ch, path, "local nixpkgs");
        if (verbose) {
            fprintf(stderr, "nix-search: nixpkgs found via NIX_PATH: %s\n",
                    path);
        }
    }
    free(out);
    free(err);
    return ok;
}

static char *registry_value_for_nixpkgs(void)
{
    const char *argv[] = {nix_bin(), "registry", "list", NULL};
    char *out = NULL;
    char *err = NULL;
    int code = -1;
    if (run_capture(argv, &out, &err, &code) != 0 || code != 0) {
        free(out);
        free(err);
        return NULL;
    }
    free(err);

    char *save = NULL;
    for (char *line = strtok_r(out, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        char *pos = strstr(line, "flake:nixpkgs ");
        if (pos == NULL) {
            continue;
        }
        char *value = trim(pos + strlen("flake:nixpkgs "));
        if (value[0] == '\0' || strcmp(value, "flake:nixpkgs") == 0) {
            continue;
        }
        char *result = xstrdup(value);
        free(out);
        return result;
    }

    free(out);
    return NULL;
}

static bool detect_from_registry(Channel *ch)
{
    char *value = registry_value_for_nixpkgs();
    if (value == NULL) {
        return false;
    }

    bool ok = false;
    if (starts_with(value, "path:")) {
        const char *p = value + strlen("path:");
        size_t len = strcspn(p, "?#");
        char *clean = xstrndup(p, len);
        set_local(ch, clean, "nixpkgs registry");
        free(clean);
        ok = true;
    } else if (value[0] == '/') {
        set_local(ch, value, "nixpkgs registry");
        ok = true;
    } else if (starts_with(value, "github:NixOS/nixpkgs/")) {
        const char *tail = value + strlen("github:NixOS/nixpkgs/");
        size_t len = strcspn(tail, "?#");
        char *branch = xstrndup(tail, len);
        if (branch[0] != '\0') {
            set_branch(ch, branch, "nixpkgs registry");
            ok = true;
        }
        free(branch);
    } else if (strcmp(value, "github:NixOS/nixpkgs") == 0 ||
               strcmp(value, "flake:nixpkgs") == 0) {
        set_branch(ch, "nixos-unstable", "nixpkgs registry");
        ok = true;
    }

    free(value);
    return ok;
}

static bool detect_from_home_channel(Channel *ch)
{
    const char *home = getenv_nonempty("HOME");
    if (home == NULL) {
        return false;
    }
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "%s/.nix-defexpr/channels/nixpkgs", home);
    bool ok = file_exists(sb.data);
    if (ok) {
        set_local(ch, sb.data, "user nix channel");
    }
    sb_free(&sb);
    return ok;
}

static char *url_basename(const char *url)
{
    const char *end = url + strlen(url);
    while (end > url && end[-1] == '/') {
        end--;
    }
    const char *start = end;
    while (start > url && start[-1] != '/') {
        start--;
    }
    return xstrndup(start, (size_t)(end - start));
}

static bool detect_from_channels(Channel *ch)
{
    const char *argv[] = {"nix-channel", "--list", NULL};
    char *out = NULL;
    char *err = NULL;
    int code = -1;
    if (run_capture(argv, &out, &err, &code) != 0 || code != 0) {
        free(out);
        free(err);
        return false;
    }
    free(err);

    char *nixos_url = NULL;
    char *nixpkgs_url = NULL;
    char *save = NULL;
    for (char *line = strtok_r(out, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        char *space = strpbrk(line, " \t");
        if (space == NULL) {
            continue;
        }
        *space = '\0';
        char *name = trim(line);
        char *url = trim(space + 1);
        if (strcmp(name, "nixpkgs") == 0) {
            free(nixpkgs_url);
            nixpkgs_url = xstrdup(url);
        } else if (strcmp(name, "nixos") == 0) {
            free(nixos_url);
            nixos_url = xstrdup(url);
        }
    }

    bool ok = false;
    const char *url = nixpkgs_url != NULL ? nixpkgs_url : nixos_url;
    if (url != NULL) {
        char *base = url_basename(url);
        char *branch = channel_branch_from_name(base);
        if (branch != NULL) {
            set_branch(ch, branch, "nix channel");
            free(branch);
            ok = true;
        }
        free(base);
    }

    free(nixos_url);
    free(nixpkgs_url);
    free(out);
    return ok;
}

static char *os_release_value(const char *text, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = text;
    while (*p != '\0') {
        const char *line_end = strchr(p, '\n');
        if (line_end == NULL) {
            line_end = p + strlen(p);
        }
        if ((size_t)(line_end - p) > key_len && p[key_len] == '=' &&
            strncmp(p, key, key_len) == 0) {
            const char *value = p + key_len + 1;
            size_t len = (size_t)(line_end - value);
            if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
                value++;
                len -= 2;
            }
            return xstrndup(value, len);
        }
        p = *line_end == '\n' ? line_end + 1 : line_end;
    }
    return NULL;
}

static bool detect_from_os_release(Channel *ch)
{
    char *text = read_file("/etc/os-release", 65536);
    if (text == NULL) {
        return false;
    }
    if (strstr(text, "ID=nixos") == NULL) {
        free(text);
        return false;
    }

    char *version_id = os_release_value(text, "VERSION_ID");
    char *build_id = os_release_value(text, "BUILD_ID");
    char *version_text = os_release_value(text, "VERSION");

    bool unstable = false;
    const char *markers[] = {version_id, build_id, version_text};
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if (markers[i] != NULL &&
            (strstr(markers[i], "pre") != NULL ||
             strstr(markers[i], "unstable") != NULL)) {
            unstable = true;
            break;
        }
    }

    bool ok = false;
    if (unstable) {
        set_branch(ch, "nixos-unstable", "NixOS release");
        ok = true;
    } else {
        char *version = version_id != NULL
                            ? version_id
                            : (build_id != NULL ? build_id : version_text);
        if (version != NULL) {
            char *branch = channel_branch_from_name(version);
            if (branch != NULL) {
                set_branch(ch, branch, "NixOS release");
                free(branch);
                ok = true;
            }
        }
    }

    free(version_id);
    free(build_id);
    free(version_text);
    free(text);
    return ok;
}

static bool detect_from_nixos_version(Channel *ch)
{
    const char *argv[] = {"nixos-version", NULL};
    char *out = NULL;
    char *err = NULL;
    int code = -1;
    if (run_capture(argv, &out, &err, &code) != 0 || code != 0) {
        free(out);
        free(err);
        return false;
    }
    free(err);

    bool ok = set_branch_from_name(ch, out, "NixOS release");
    free(out);
    return ok;
}

int channel_detect(Channel *ch, const char *override, bool verbose,
                   char **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    channel_init(ch);

    if (override != NULL && override[0] != '\0' &&
        strcmp(override, "auto") != 0) {
        if (looks_like_flake(override)) {
            if (channel_set_flake(ch, override) != 0) {
                if (error != NULL) {
                    *error = xstrdup("invalid flake reference");
                }
                return -1;
            }
            return 0;
        }

        char *branch = channel_branch_from_name(override);
        if (branch == NULL) {
            if (error != NULL) {
                StrBuf sb;
                sb_init(&sb);
                sb_printf(&sb,
                          "invalid channel '%s' (try 25.05, unstable, "
                          "nixos-25.05 or github:NixOS/nixpkgs/nixos-25.05)",
                          override);
                *error = sb_steal(&sb);
            }
            return -1;
        }
        set_branch(ch, branch, "requested channel");
        free(branch);
        return 0;
    }

    if (detect_from_find_file(ch, verbose)) {
        return 0;
    }
    if (detect_from_home_channel(ch)) {
        return 0;
    }
    if (detect_from_registry(ch)) {
        return 0;
    }
    if (detect_from_channels(ch)) {
        return 0;
    }
    if (detect_from_os_release(ch)) {
        return 0;
    }
    if (detect_from_nixos_version(ch)) {
        return 0;
    }

    set_branch(ch, "nixos-unstable", "fallback (no channel detected)");
    return 0;
}
