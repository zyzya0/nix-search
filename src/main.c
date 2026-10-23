#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "channel.h"
#include "util.h"

static char *get_system(void)
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        return xstrdup("x86_64-linux");
    }

    if (strcmp(uts.sysname, "Darwin") == 0) {
        return xstrdup(strcmp(uts.machine, "arm64") == 0 ? "aarch64-darwin"
                                                          : "x86_64-darwin");
    }

    if (strcmp(uts.machine, "x86_64") == 0) {
        return xstrdup("x86_64-linux");
    }
    if (strcmp(uts.machine, "aarch64") == 0 ||
        strcmp(uts.machine, "arm64") == 0) {
        return xstrdup("aarch64-linux");
    }
    if (strcmp(uts.machine, "i686") == 0 ||
        strcmp(uts.machine, "i386") == 0) {
        return xstrdup("i686-linux");
    }
    if (strcmp(uts.machine, "armv7l") == 0) {
        return xstrdup("armv7l-linux");
    }

    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "%s-linux", uts.machine);
    return sb_steal(&sb);
}

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: nix-search PACKAGE\n"
            "\n"
            "Check whether PACKAGE exists in the nixpkgs channel used by\n"
            "this system.\n"
            "\n"
            "Output:\n"
            "  есть               package exists\n"
            "  такого пакета нет  package does not exist\n"
            "\n"
            "Exit status: 0 if found, 1 if not found, 2 on error.\n"
            "\n"
            "Options:\n"
            "  -h, --help     show this help\n"
            "  -V, --version  show version\n");
}

static bool package_in_attrset(const Channel *channel, const char *system,
                               const char *attrset, const char *query)
{
    StrBuf expr;
    sb_init(&expr);
    sb_printf(&expr, "%s#%s.%s.%s.outPath", channel->ref, attrset, system,
              query);

    const char *argv[] = {
        nix_bin(),
        "--extra-experimental-features",
        "nix-command flakes",
        "eval",
        "--quiet",
        "--raw",
        "--no-warn-dirty",
        expr.data,
        NULL,
    };

    char *out = NULL;
    char *err = NULL;
    int exit_code = -1;
    int status = run_capture(argv, &out, &err, &exit_code);
    bool found =
        status == 0 && exit_code == 0 && out != NULL && out[0] != '\0';

    free(out);
    free(err);
    sb_free(&expr);
    return found;
}

int main(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                       strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-V") == 0 ||
                       strcmp(argv[1], "--version") == 0)) {
        printf("nix-search %s\n", NS_VERSION);
        return 0;
    }
    if (argc != 2 || argv[1][0] == '\0') {
        usage(stderr);
        return 2;
    }

    const char *query = argv[1];

    Channel channel;
    channel_init(&channel);
    char *channel_error = NULL;
    if (channel_detect(&channel, NULL, false, &channel_error) != 0) {
        fprintf(stderr, "nix-search: %s\n",
                channel_error != NULL ? channel_error
                                      : "failed to detect nixpkgs channel");
        free(channel_error);
        channel_free(&channel);
        return 2;
    }

    char *system = get_system();
    bool found = package_in_attrset(&channel, system, "legacyPackages", query);
    if (!found) {
        found = package_in_attrset(&channel, system, "packages", query);
    }

    printf("%s\n", found ? "есть" : "такого пакета нет");

    free(system);
    channel_free(&channel);
    return found ? 0 : 1;
}
