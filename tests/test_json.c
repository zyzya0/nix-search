#include "json.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_object(void)
{
    const char *text =
        "{\"found\":true,\"name\":\"hello\",\"version\":\"2.12.3\","
        "\"deps\":[\"a\",\"b\"],\"unicode\":\"\\u0442\\u0435\\u0441\\u0442\"}";
    char *error = NULL;
    JsonValue *root = json_parse(text, &error);
    assert(root != NULL);
    assert(error == NULL);

    const JsonValue *found = json_object_get(root, "found");
    assert(found != NULL);
    assert(json_bool_value(found, false));

    const JsonValue *name = json_object_get(root, "name");
    assert(name != NULL);
    assert(strcmp(json_string_value(name), "hello") == 0);

    const JsonValue *deps = json_object_get(root, "deps");
    assert(json_array_length(deps) == 2);
    assert(strcmp(json_string_value(json_array_get(deps, 0)), "a") == 0);
    assert(strcmp(json_string_value(json_array_get(deps, 1)), "b") == 0);

    const JsonValue *unicode = json_object_get(root, "unicode");
    assert(strcmp(json_string_value(unicode), "тест") == 0);

    assert(json_object_get(root, "missing") == NULL);
    json_free(root);
}

static void test_nested(void)
{
    char *error = NULL;
    JsonValue *root = json_parse(
        "{\"array\":[1,2,{\"x\":\"y\"}],\"object\":{\"flag\":false}}", &error);
    assert(root != NULL);
    assert(error == NULL);

    const JsonValue *array = json_object_get(root, "array");
    assert(json_array_length(array) == 3);
    const JsonValue *inner = json_array_get(array, 2);
    const JsonValue *x = json_object_get(inner, "x");
    assert(strcmp(json_string_value(x), "y") == 0);

    const JsonValue *object = json_object_get(root, "object");
    const JsonValue *flag = json_object_get(object, "flag");
    assert(flag != NULL);
    assert(!json_bool_value(flag, true));

    json_free(root);
}

static void test_errors(void)
{
    char *error = NULL;
    JsonValue *value = json_parse("{\"found\":true", &error);
    assert(value == NULL);
    assert(error != NULL);
    free(error);

    error = NULL;
    value = json_parse("{} trailing", &error);
    assert(value == NULL);
    assert(error != NULL);
    free(error);

    error = NULL;
    value = json_parse("[1,]", &error);
    assert(value == NULL);
    assert(error != NULL);
    free(error);
}

int main(void)
{
    test_object();
    test_nested();
    test_errors();
    puts("test_json: ok");
    return 0;
}
