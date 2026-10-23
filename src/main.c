#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include "channel.h"
#include "package.h"
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
            "Show information about PACKAGE in the nixpkgs channel used by\n"
            "this system.\n"
            "\n"
            "PACKAGE can be a plain name (hello) or an attribute path\n"
            "(python3Packages.requests).\n"
            "\n"
            "Output:\n"
            "  package metadata and dependencies, or\n"
            "  \"такого пакета нет\" when the package does not exist.\n"
            "\n"
            "Exit status: 0 if found, 1 if not found, 2 on error.\n"
            "\n"
            "Options:\n"
            "  -h, --help     show this help\n"
            "  -V, --version  show version\n");
}

static void print_dependency_group(const char *label, char *const *items,
                                   size_t len)
{
    if (len == 0) {
        printf("  %s: (нет)\n", label);
        return;
    }

    printf("  %s (%zu):\n", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("    - %s\n", items[i]);
    }
}

static void print_package_info(const PackageInfo *info)
{
    printf("Пакет:       %s\n", info->name);
    if (info->full_name[0] != '\0') {
        printf("Полное имя:  %s\n", info->full_name);
    }
    printf("Версия:      %s\n",
           info->version[0] != '\0' ? info->version : "неизвестно");
    printf("Описание:    %s\n",
           info->description[0] != '\0' ? info->description : "нет описания");
    printf("\nЗависимости:\n");
    print_dependency_group("build", info->build_inputs,
                           info->build_inputs_len);
    print_dependency_group("native", info->native_inputs,
                           info->native_inputs_len);
    print_dependency_group("propagated", info->propagated_inputs,
                           info->propagated_inputs_len);
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
    char *name_error = NULL;
    if (!package_name_valid(query, &name_error)) {
        fprintf(stderr, "nix-search: %s\n",
                name_error != NULL ? name_error : "некорректное имя пакета");
        free(name_error);
        return 2;
    }

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
    PackageInfo info;
    package_info_init(&info);
    char *package_error = NULL;
    PackageResult result =
        package_lookup(&channel, system, query, &info, &package_error);

    int exit_status = 0;
    if (result == PACKAGE_FOUND) {
        print_package_info(&info);
    } else if (result == PACKAGE_NOT_FOUND) {
        printf("такого пакета нет\n");
        exit_status = 1;
    } else {
        fprintf(stderr, "nix-search: %s\n",
                package_error != NULL ? package_error
                                      : "не удалось получить данные о пакете");
        exit_status = 2;
    }

    free(package_error);
    package_info_free(&info);
    free(system);
    channel_free(&channel);
    return exit_status;
}
