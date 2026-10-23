#include "json.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct {
    const char *text;
    size_t len;
    size_t pos;
    char **error;
} Parser;

static void parser_error(Parser *p, const char *message)
{
    if (p->error != NULL && *p->error == NULL) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "JSON: %s at byte %zu", message, p->pos);
        *p->error = sb_steal(&sb);
    }
}

static JsonValue *json_new(JsonType type)
{
    JsonValue *value = xcalloc(1, sizeof(*value));
    value->type = type;
    return value;
}

static void skip_ws(Parser *p)
{
    while (p->pos < p->len &&
           (p->text[p->pos] == ' ' || p->text[p->pos] == '\t' ||
            p->text[p->pos] == '\n' || p->text[p->pos] == '\r')) {
        p->pos++;
    }
}

static void append_utf8(StrBuf *sb, unsigned codepoint)
{
    if (codepoint <= 0x7f) {
        sb_putc(sb, (char)codepoint);
    } else if (codepoint <= 0x7ff) {
        sb_putc(sb, (char)(0xc0 | (codepoint >> 6)));
        sb_putc(sb, (char)(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        sb_putc(sb, (char)(0xe0 | (codepoint >> 12)));
        sb_putc(sb, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
        sb_putc(sb, (char)(0x80 | (codepoint & 0x3f)));
    } else {
        sb_putc(sb, (char)(0xf0 | (codepoint >> 18)));
        sb_putc(sb, (char)(0x80 | ((codepoint >> 12) & 0x3f)));
        sb_putc(sb, (char)(0x80 | ((codepoint >> 6) & 0x3f)));
        sb_putc(sb, (char)(0x80 | (codepoint & 0x3f)));
    }
}

static bool parse_hex4(Parser *p, unsigned *out)
{
    if (p->pos + 4 > p->len) {
        return false;
    }

    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->text[p->pos++];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (unsigned)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (unsigned)(c - 'A' + 10);
        } else {
            return false;
        }
    }
    *out = value;
    return true;
}

static char *parse_string_content(Parser *p)
{
    if (p->pos >= p->len || p->text[p->pos] != '"') {
        parser_error(p, "expected string");
        return NULL;
    }
    p->pos++;

    StrBuf sb;
    sb_init(&sb);

    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->text[p->pos++];
        if (c == '"') {
            return sb_steal(&sb);
        }
        if (c == '\\') {
            if (p->pos >= p->len) {
                break;
            }
            char esc = p->text[p->pos++];
            switch (esc) {
            case '"':
                sb_putc(&sb, '"');
                break;
            case '\\':
                sb_putc(&sb, '\\');
                break;
            case '/':
                sb_putc(&sb, '/');
                break;
            case 'b':
                sb_putc(&sb, '\b');
                break;
            case 'f':
                sb_putc(&sb, '\f');
                break;
            case 'n':
                sb_putc(&sb, '\n');
                break;
            case 'r':
                sb_putc(&sb, '\r');
                break;
            case 't':
                sb_putc(&sb, '\t');
                break;
            case 'u': {
                unsigned codepoint;
                if (!parse_hex4(p, &codepoint)) {
                    parser_error(p, "invalid unicode escape");
                    sb_free(&sb);
                    return NULL;
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (p->pos + 6 <= p->len && p->text[p->pos] == '\\' &&
                        p->text[p->pos + 1] == 'u') {
                        p->pos += 2;
                        unsigned low;
                        if (!parse_hex4(p, &low) || low < 0xdc00 ||
                            low > 0xdfff) {
                            parser_error(p, "invalid surrogate pair");
                            sb_free(&sb);
                            return NULL;
                        }
                        codepoint =
                            0x10000 + ((codepoint - 0xd800) << 10) +
                            (low - 0xdc00);
                    } else {
                        codepoint = 0xfffd;
                    }
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    codepoint = 0xfffd;
                }
                if (codepoint == 0) {
                    parser_error(p, "NUL is not supported in strings");
                    sb_free(&sb);
                    return NULL;
                }
                append_utf8(&sb, codepoint);
                break;
            }
            default:
                parser_error(p, "invalid escape sequence");
                sb_free(&sb);
                return NULL;
            }
        } else if (c < 0x20) {
            parser_error(p, "control character in string");
            sb_free(&sb);
            return NULL;
        } else {
            sb_putc(&sb, (char)c);
        }
    }

    parser_error(p, "unterminated string");
    sb_free(&sb);
    return NULL;
}

static JsonValue *parse_value(Parser *p, unsigned depth);

static bool array_append(JsonValue *array, JsonValue *item)
{
    if (array->u.array.len == array->u.array.cap) {
        size_t cap = array->u.array.cap == 0 ? 4 : array->u.array.cap * 2;
        array->u.array.items =
            xrealloc(array->u.array.items, cap * sizeof(*array->u.array.items));
        array->u.array.cap = cap;
    }
    array->u.array.items[array->u.array.len++] = item;
    return true;
}

static bool object_append(JsonValue *object, char *key, JsonValue *value)
{
    if (object->u.object.len == object->u.object.cap) {
        size_t cap =
            object->u.object.cap == 0 ? 4 : object->u.object.cap * 2;
        object->u.object.keys =
            xrealloc(object->u.object.keys, cap * sizeof(*object->u.object.keys));
        object->u.object.values = xrealloc(
            object->u.object.values, cap * sizeof(*object->u.object.values));
        object->u.object.cap = cap;
    }
    object->u.object.keys[object->u.object.len] = key;
    object->u.object.values[object->u.object.len] = value;
    object->u.object.len++;
    return true;
}

static JsonValue *parse_array(Parser *p, unsigned depth)
{
    JsonValue *array = json_new(JSON_ARRAY);
    p->pos++;
    skip_ws(p);

    if (p->pos < p->len && p->text[p->pos] == ']') {
        p->pos++;
        return array;
    }

    for (;;) {
        JsonValue *item = parse_value(p, depth + 1);
        if (item == NULL) {
            json_free(array);
            return NULL;
        }
        array_append(array, item);
        skip_ws(p);
        if (p->pos >= p->len) {
            parser_error(p, "unterminated array");
            json_free(array);
            return NULL;
        }
        char c = p->text[p->pos++];
        if (c == ']') {
            return array;
        }
        if (c != ',') {
            parser_error(p, "expected ',' or ']'");
            json_free(array);
            return NULL;
        }
        skip_ws(p);
    }
}

static JsonValue *parse_object(Parser *p, unsigned depth)
{
    JsonValue *object = json_new(JSON_OBJECT);
    p->pos++;
    skip_ws(p);

    if (p->pos < p->len && p->text[p->pos] == '}') {
        p->pos++;
        return object;
    }

    for (;;) {
        skip_ws(p);
        char *key = parse_string_content(p);
        if (key == NULL) {
            json_free(object);
            return NULL;
        }
        skip_ws(p);
        if (p->pos >= p->len || p->text[p->pos] != ':') {
            parser_error(p, "expected ':'");
            free(key);
            json_free(object);
            return NULL;
        }
        p->pos++;
        JsonValue *value = parse_value(p, depth + 1);
        if (value == NULL) {
            free(key);
            json_free(object);
            return NULL;
        }
        object_append(object, key, value);

        skip_ws(p);
        if (p->pos >= p->len) {
            parser_error(p, "unterminated object");
            json_free(object);
            return NULL;
        }
        char c = p->text[p->pos++];
        if (c == '}') {
            return object;
        }
        if (c != ',') {
            parser_error(p, "expected ',' or '}'");
            json_free(object);
            return NULL;
        }
    }
}

static JsonValue *parse_number(Parser *p)
{
    size_t start = p->pos;
    if (p->text[p->pos] == '-') {
        p->pos++;
    }
    if (p->pos >= p->len || !isdigit((unsigned char)p->text[p->pos])) {
        parser_error(p, "invalid number");
        return NULL;
    }

    if (p->text[p->pos] == '0') {
        p->pos++;
    } else {
        while (p->pos < p->len &&
               isdigit((unsigned char)p->text[p->pos])) {
            p->pos++;
        }
    }

    if (p->pos < p->len && p->text[p->pos] == '.') {
        p->pos++;
        if (p->pos >= p->len || !isdigit((unsigned char)p->text[p->pos])) {
            parser_error(p, "invalid number fraction");
            return NULL;
        }
        while (p->pos < p->len &&
               isdigit((unsigned char)p->text[p->pos])) {
            p->pos++;
        }
    }

    if (p->pos < p->len &&
        (p->text[p->pos] == 'e' || p->text[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len &&
            (p->text[p->pos] == '+' || p->text[p->pos] == '-')) {
            p->pos++;
        }
        if (p->pos >= p->len || !isdigit((unsigned char)p->text[p->pos])) {
            parser_error(p, "invalid number exponent");
            return NULL;
        }
        while (p->pos < p->len &&
               isdigit((unsigned char)p->text[p->pos])) {
            p->pos++;
        }
    }

    char *end = NULL;
    double number = strtod(p->text + start, &end);
    if (end != p->text + p->pos) {
        parser_error(p, "invalid number");
        return NULL;
    }

    JsonValue *value = json_new(JSON_NUMBER);
    value->u.number = number;
    return value;
}

static JsonValue *parse_value(Parser *p, unsigned depth)
{
    if (depth > 128) {
        parser_error(p, "nesting is too deep");
        return NULL;
    }

    skip_ws(p);
    if (p->pos >= p->len) {
        parser_error(p, "unexpected end of input");
        return NULL;
    }

    char c = p->text[p->pos];
    switch (c) {
    case 'n':
        if (p->pos + 4 <= p->len &&
            strncmp(p->text + p->pos, "null", 4) == 0) {
            p->pos += 4;
            return json_new(JSON_NULL);
        }
        break;
    case 't':
        if (p->pos + 4 <= p->len &&
            strncmp(p->text + p->pos, "true", 4) == 0) {
            p->pos += 4;
            JsonValue *value = json_new(JSON_BOOL);
            value->u.boolean = true;
            return value;
        }
        break;
    case 'f':
        if (p->pos + 5 <= p->len &&
            strncmp(p->text + p->pos, "false", 5) == 0) {
            p->pos += 5;
            JsonValue *value = json_new(JSON_BOOL);
            value->u.boolean = false;
            return value;
        }
        break;
    case '"': {
        char *string = parse_string_content(p);
        if (string == NULL) {
            return NULL;
        }
        JsonValue *value = json_new(JSON_STRING);
        value->u.string = string;
        return value;
    }
    case '[':
        return parse_array(p, depth);
    case '{':
        return parse_object(p, depth);
    default:
        if (c == '-' || isdigit((unsigned char)c)) {
            return parse_number(p);
        }
        break;
    }

    parser_error(p, "unexpected character");
    return NULL;
}

JsonValue *json_parse(const char *text, char **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (text == NULL) {
        if (error != NULL) {
            *error = xstrdup("JSON: empty input");
        }
        return NULL;
    }

    Parser p = {
        .text = text,
        .len = strlen(text),
        .pos = 0,
        .error = error,
    };

    JsonValue *value = parse_value(&p, 0);
    if (value == NULL) {
        return NULL;
    }

    skip_ws(&p);
    if (p.pos != p.len) {
        parser_error(&p, "trailing characters");
        json_free(value);
        return NULL;
    }
    return value;
}

void json_free(JsonValue *value)
{
    if (value == NULL) {
        return;
    }

    switch (value->type) {
    case JSON_STRING:
        free(value->u.string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < value->u.array.len; i++) {
            json_free(value->u.array.items[i]);
        }
        free(value->u.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < value->u.object.len; i++) {
            free(value->u.object.keys[i]);
            json_free(value->u.object.values[i]);
        }
        free(value->u.object.keys);
        free(value->u.object.values);
        break;
    default:
        break;
    }
    free(value);
}

const JsonValue *json_object_get(const JsonValue *object, const char *key)
{
    if (object == NULL || object->type != JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < object->u.object.len; i++) {
        if (strcmp(object->u.object.keys[i], key) == 0) {
            return object->u.object.values[i];
        }
    }
    return NULL;
}

const char *json_string_value(const JsonValue *value)
{
    return value != NULL && value->type == JSON_STRING ? value->u.string
                                                       : NULL;
}

bool json_bool_value(const JsonValue *value, bool fallback)
{
    return value != NULL && value->type == JSON_BOOL ? value->u.boolean
                                                     : fallback;
}

size_t json_array_length(const JsonValue *value)
{
    return value != NULL && value->type == JSON_ARRAY ? value->u.array.len
                                                      : 0;
}

const JsonValue *json_array_get(const JsonValue *value, size_t index)
{
    if (value == NULL || value->type != JSON_ARRAY ||
        index >= value->u.array.len) {
        return NULL;
    }
    return value->u.array.items[index];
}
