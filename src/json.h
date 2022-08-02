#pragma once

#include "common.h"

enum JsonType {
    JSON_NULL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_BOOLEAN,
    JSON_ARRAY,
    JSON_OBJECT,
};

struct Json;

struct JsonPair {
    char* name;
    Json* json;
    JsonPair* next;
};

struct Json {
    JsonType type;
    union {
        float number;
        char* string;
        bool boolean;
        Json* arr_first;
        JsonPair* obj_first;
    };
    Json* next;
};

Json* json_parse(char* str);
void json_free(Json* j);

Json* json_lookup(Json* j, char* name);
bool json_has(Json* j, char* name);

float json_number(Json* j);
char* json_string(Json* j);
bool json_boolean(Json* j);

int json_array_len(Json* j);

#define JSON_ARRAY_FOR(arr, el) assert(arr->type == JSON_ARRAY); \
                                for (Json* el = arr->arr_first; el; el = el->next)
