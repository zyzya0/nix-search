#include "package.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "util.h"

#define ENV_FLAKE "NIX_SEARCH_META_FLAKE"
#define ENV_SYSTEM "NIX_SEARCH_META_SYSTEM"
#define ENV_NAME "NIX_SEARCH_META_NAME"

/*
 * The package name is not interpolated into this expression.  It is
 * read from the environment with builtins.getEnv, so quoting and Nix
 * syntax cannot be injected by the query.
 */
static const char *META_EXPR =
    "let\n"
    "  flakeRef = builtins.getEnv \"NIX_SEARCH_META_FLAKE\";\n"
    "  system = builtins.getEnv \"NIX_SEARCH_META_SYSTEM\";\n"
    "  name = builtins.getEnv \"NIX_SEARCH_META_NAME\";\n"
    "  flake = builtins.getFlake flakeRef;\n"
    "  legacy = flake.legacyPackages.${system} or {};\n"
    "  packages = flake.packages.${system} or {};\n"
    "  parts = builtins.filter builtins.isString (builtins.split \"\\\\.\" name);\n"
    "  pick = set: parts:\n"
    "    if parts == [] then set\n"
    "    else if !(builtins.isAttrs set) then null\n"
    "    else\n"
    "      let\n"
    "        head = builtins.head parts;\n"
    "        tail = builtins.tail parts;\n"
    "      in\n"
    "        if builtins.hasAttr head set\n"
    "        then pick (builtins.getAttr head set) tail\n"
    "        else null;\n"
    "  fromLegacy = pick legacy parts;\n"
    "  fromPackages = pick packages parts;\n"
    "  pkg = if fromLegacy != null then fromLegacy else fromPackages;\n"
    "  depNames = list:\n"
    "    builtins.filter (x: x != null) (builtins.map (d:\n"
    "      if builtins.isAttrs d && d ? name then d.name\n"
    "      else if builtins.isAttrs d && d ? pname then d.pname\n"
    "      else null) (if builtins.isList list then list else []));\n"
    "in\n"
    "if pkg == null || !(builtins.isAttrs pkg) || (pkg.type or \"\") != \"derivation\"\n"
    "then { found = false; }\n"
    "else {\n"
    "  found = true;\n"
    "  name = pkg.pname or (pkg.name or name);\n"
    "  fullName = pkg.name or \"\";\n"
    "  version = pkg.version or \"\";\n"
    "  description = pkg.meta.description or \"\";\n"
    "  buildInputs = depNames (pkg.buildInputs or []);\n"
    "  nativeBuildInputs = depNames (pkg.nativeBuildInputs or []);\n"
    "  propagatedBuildInputs = depNames (pkg.propagatedBuildInputs or []);\n"
    "}\n";

static void set_error(char **error, const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = xstrdup(message);
    }
}

static bool invalid_name(char **error, const char *message)
{
    set_error(error, message);
    return false;
}

void package_info_init(PackageInfo *info)
{
    memset(info, 0, sizeof(*info));
}

static void free_string_array(char **items, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        free(items[i]);
    }
    free(items);
}

void package_info_free(PackageInfo *info)
{
    free(info->name);
    free(info->full_name);
    free(info->version);
    free(info->description);
    free_string_array(info->build_inputs, info->build_inputs_len);
    free_string_array(info->native_inputs, info->native_inputs_len);
    free_string_array(info->propagated_inputs, info->propagated_inputs_len);
    package_info_init(info);
}

bool package_name_valid(const char *name, char **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (name == NULL || name[0] == '\0') {
        return invalid_name(error, "пустое имя пакета");
    }

    size_t total = strlen(name);
    if (total > 512) {
        return invalid_name(error, "слишком длинное имя пакета");
    }

    size_t component_len = 0;
    for (const char *p = name;; p++) {
        unsigned char c = (unsigned char)*p;

        if (c == '\0' || c == '.') {
            if (component_len == 0) {
                return invalid_name(
                    error, "некорректное имя пакета: пустая часть между точками");
            }
            if (c == '\0') {
                break;
            }
            component_len = 0;
            continue;
        }

        if (c <= 0x20 || c == 0x7f) {
            return invalid_name(error,
                                "некорректный символ в имени пакета");
        }
        if (component_len >= 128) {
            return invalid_name(error, "слишком длинная часть имени пакета");
        }
        component_len++;
    }

    return true;
}

static void unset_meta_env(void)
{
    unsetenv(ENV_FLAKE);
    unsetenv(ENV_SYSTEM);
    unsetenv(ENV_NAME);
}

static bool set_meta_env(const Channel *channel, const char *system,
                         const char *name, char **error)
{
    if (setenv(ENV_FLAKE, channel->ref, 1) != 0 ||
        setenv(ENV_SYSTEM, system, 1) != 0 ||
        setenv(ENV_NAME, name, 1) != 0) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "не удалось передать параметры nix: %s",
                  strerror(errno));
        set_error(error, sb.data);
        sb_free(&sb);
        unset_meta_env();
        return false;
    }
    return true;
}

static bool copy_string_array(const JsonValue *root, const char *key,
                              char ***out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;

    const JsonValue *array = json_object_get(root, key);
    if (array == NULL || array->type != JSON_ARRAY) {
        return true;
    }

    size_t count = json_array_length(array);
    char **items = xcalloc(count == 0 ? 1 : count, sizeof(*items));
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        const char *value = json_string_value(json_array_get(array, i));
        if (value != NULL) {
            items[used++] = xstrdup(value);
        }
    }

    *out = items;
    *out_len = used;
    return true;
}

static char *string_field(const JsonValue *root, const char *key,
                          const char *fallback)
{
    const char *value = json_string_value(json_object_get(root, key));
    if (value == NULL || value[0] == '\0') {
        return xstrdup(fallback);
    }
    return xstrdup(value);
}

PackageResult package_lookup(const Channel *channel, const char *system,
                             const char *name, PackageInfo *info,
                             char **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (info == NULL) {
        set_error(error, "внутренняя ошибка: не задана структура пакета");
        return PACKAGE_ERROR;
    }
    package_info_free(info);

    if (channel == NULL || channel->ref == NULL || system == NULL ||
        name == NULL) {
        set_error(error, "внутренняя ошибка: не задан канал или система");
        return PACKAGE_ERROR;
    }

    if (!set_meta_env(channel, system, name, error)) {
        return PACKAGE_ERROR;
    }

    const char *argv[] = {
        nix_bin(),
        "--extra-experimental-features",
        "nix-command flakes",
        "eval",
        "--quiet",
        "--impure",
        "--json",
        "--no-warn-dirty",
        "--expr",
        META_EXPR,
        NULL,
    };

    char *out = NULL;
    char *err = NULL;
    int exit_code = -1;
    int status = run_capture(argv, &out, &err, &exit_code);
    unset_meta_env();

    if (status != 0) {
        set_error(error, "не удалось запустить nix");
        free(out);
        free(err);
        return PACKAGE_ERROR;
    }

    if (exit_code != 0) {
        const char *message = "nix eval завершился с ошибкой";
        if (err != NULL && err[0] != '\0') {
            message = trim(err);
        }
        set_error(error, message);
        free(out);
        free(err);
        return PACKAGE_ERROR;
    }

    char *json_error = NULL;
    JsonValue *root = json_parse(out != NULL ? out : "", &json_error);
    free(out);
    free(err);
    if (root == NULL) {
        set_error(error, json_error != NULL ? json_error
                                            : "не удалось разобрать JSON от nix");
        free(json_error);
        return PACKAGE_ERROR;
    }

    const JsonValue *found = json_object_get(root, "found");
    if (found == NULL || found->type != JSON_BOOL) {
        json_free(root);
        set_error(error, "nix eval вернул неожиданный JSON");
        return PACKAGE_ERROR;
    }

    if (!found->u.boolean) {
        json_free(root);
        return PACKAGE_NOT_FOUND;
    }

    info->name = string_field(root, "name", name);
    info->full_name = string_field(root, "fullName", "");
    info->version = string_field(root, "version", "");
    info->description = string_field(root, "description", "");

    copy_string_array(root, "buildInputs", &info->build_inputs,
                      &info->build_inputs_len);
    copy_string_array(root, "nativeBuildInputs", &info->native_inputs,
                      &info->native_inputs_len);
    copy_string_array(root, "propagatedBuildInputs",
                      &info->propagated_inputs,
                      &info->propagated_inputs_len);

    json_free(root);
    return PACKAGE_FOUND;
}
