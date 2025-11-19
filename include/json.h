#ifndef CGOTPL_JSON
#define CGOTPL_JSON

#include <stdio.h>

#include "map.h"
#include "stream.h"

#define JSON_TY_NULL 0  // keep in sync with JSON_NULL;
#define JSON_TY_OBJECT 1
#define JSON_TY_ARRAY 2
#define JSON_TY_NUMBER 3
#define JSON_TY_STRING 4
#define JSON_TY_FALSE 5
#define JSON_TY_TRUE 6

struct json_value_st;
typedef struct json_value_st json_value;

typedef struct {
    json_value* data;
    size_t len;
    size_t cap;
} json_array;

struct json_value_st {
    int ty;
    union {
        hashmap obj;
        char* str;
        double num;
        json_array arr;
    } inner;
};

#define JSON_NULL (json_value){.ty = 0}

#define ERR_JSON_INVALID_ESCAPE -800
#define ERR_JSON_INVALID_SYNTAX -801
#define ERR_JSON_BUFFER_OVERFLOW -802
#define ERR_JSON_DEPTH_EXCEEDED -803

// Consumes an abitrary amount of bytes from st to parse a single JSON value
// into val. Returns 0 on success.
int json_parse(stream* st, json_value* val);
void json_value_copy(json_value* dest, const json_value* src);
int json_value_equal(const json_value* a, const json_value* b);
void json_value_free(json_value* val);

char* json_describe_err(int err);

#endif
