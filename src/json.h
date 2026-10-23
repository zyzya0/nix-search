#ifndef NIX_SEARCH_JSON_H
#define NIX_SEARCH_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct {
            JsonValue **items;
            size_t len;
            size_t cap;
        } array;
        struct {
            char **keys;
            JsonValue **values;
            size_t len;
            size_t cap;
        } object;
    } u;
};

JsonValue *json_parse(const char *text, char **error);
void json_free(JsonValue *value);

const JsonValue *json_object_get(const JsonValue *object, const char *key);
const char *json_string_value(const JsonValue *value);
bool json_bool_value(const JsonValue *value, bool fallback);
size_t json_array_length(const JsonValue *value);
const JsonValue *json_array_get(const JsonValue *value, size_t index);

#endif
