#include "json.h"

#include <stdio.h>
#include <string.h>

#include "map.h"
#include "stream.h"
#include "test.h"

nutest_result json_parse_str_ascii(void) {
    stream st;
    stream_open_memory(&st, "\"xyz\"", 6);
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_STRING);
    NUTEST_ASSERT(strlen(val.inner.str) == 3);
    NUTEST_ASSERT(strcmp(val.inner.str, "xyz") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_str_long(void) {
    size_t data_len = 64;
    char* data = malloc(data_len * sizeof(char));
    data[0] = '"';
    for (size_t i = 1; i < data_len - 1; i++) {
        data[i] = 'A';
    }
    data[data_len - 1] = '"';
    stream st;
    stream_open_memory(&st, data, data_len);
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_STRING);
    NUTEST_ASSERT(strlen(val.inner.str) == 62);
    json_value_free(&val);
    stream_close(&st);
    free(data);
    return NUTEST_PASS;
}

nutest_result json_parse_str_escape(void) {
    const char* in = "\"\\\" \\\\ \\/ \\t \\n \"";
    stream st;
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_STRING);
    NUTEST_ASSERT(strcmp(val.inner.str, "\" \\ / \t \n ") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_str_utf8_escape(void) {
    const char* in = "\"\\u20ac\"";
    stream st;
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_STRING);
    NUTEST_ASSERT(strcmp(val.inner.str, "€") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_number_generic(const char* in, double expected) {
    stream st;
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_NUMBER);
    NUTEST_ASSERT(val.inner.num == expected);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_number_positive(void) {
    return json_parse_number_generic("16.25", 16.25);
}

nutest_result json_parse_number_frac(void) {
    return json_parse_number_generic("0.75", 0.75);
}

nutest_result json_parse_number_negative(void) {
    return json_parse_number_generic("-18.25", -18.25);
}

nutest_result json_parse_number_negative_frac(void) {
    return json_parse_number_generic("-0.5", -0.5);
}

nutest_result json_parse_number_zero(void) {
    return json_parse_number_generic("0", 0);
}

nutest_result json_parse_number_negative_zero(void) {
    return json_parse_number_generic("-0", -0);
}

nutest_result json_parse_number_e(void) {
    return json_parse_number_generic("1e2", 100);
}

nutest_result json_parse_number_fail(const char* in) {
    stream st;
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err != 0);
    return NUTEST_PASS;
}

nutest_result json_parse_number_leading_zero(void) {
    return json_parse_number_fail("07");
}

nutest_result json_parse_number_negative_leading_zero(void) {
    return json_parse_number_fail("-07");
}

nutest_result json_parse_number_plus(void) {
    return json_parse_number_fail("+");
}

nutest_result json_parse_number_minus(void) {
    return json_parse_number_fail("-");
}

nutest_result json_parse_number_hex(void) {
    return json_parse_number_fail("0xa2");
}

nutest_result json_parse_number_double_dot(void) {
    return json_parse_number_fail("123.456.789");
}

nutest_result json_parse_number_double_e(void) {
    return json_parse_number_fail("123e5e2");
}

nutest_result json_parse_array_empty(void) {
    stream st;
    stream_open_memory(&st, " [ ]", 4);
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_ARRAY);
    NUTEST_ASSERT(val.inner.arr.data == NULL);
    NUTEST_ASSERT(val.inner.arr.cap == 0);
    NUTEST_ASSERT(val.inner.arr.len == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_array_numbers(void) {
    stream st;
    const char* in = "[3.5, 17]";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_ARRAY);
    NUTEST_ASSERT(val.inner.arr.data != NULL);
    NUTEST_ASSERT(val.inner.arr.len == 2);
    NUTEST_ASSERT(val.inner.arr.cap >= val.inner.arr.len);
    NUTEST_ASSERT(val.inner.arr.data[0].inner.num == 3.5);
    NUTEST_ASSERT(val.inner.arr.data[1].inner.num == 17);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_array_nested(void) {
    stream st;
    const char* in = "[[\"a\", \"b\"]]";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_ARRAY);
    NUTEST_ASSERT(val.inner.arr.data != NULL);
    NUTEST_ASSERT(val.inner.arr.len == 1);
    NUTEST_ASSERT(val.inner.arr.cap >= val.inner.arr.len);
    json_value* nested = val.inner.arr.data;
    NUTEST_ASSERT(nested[0].ty == JSON_TY_ARRAY);
    NUTEST_ASSERT(strcmp(nested[0].inner.arr.data[0].inner.str, "a") == 0);
    NUTEST_ASSERT(strcmp(nested[0].inner.arr.data[1].inner.str, "b") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_obj_empty(void) {
    stream st;
    const char* in = "{}";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_OBJECT);
    NUTEST_ASSERT(val.inner.obj.count == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_obj_str_single(void) {
    stream st;
    const char* in = "{\"c\": \"d\"}";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_OBJECT);
    NUTEST_ASSERT(val.inner.obj.count == 1);
    json_value* out;
    int found = hashmap_get(&val.inner.obj, "c", (const void**)&out);
    NUTEST_ASSERT(found == 1);
    NUTEST_ASSERT(strcmp(out->inner.str, "d") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_obj_str_multi(void) {
    stream st;
    const char* in = "{\"e\": \"f\", \"ee\": \"ff\", \"eee\": \"fff\"}";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_OBJECT);
    NUTEST_ASSERT(val.inner.obj.count == 3);
    json_value* out;
    int found = hashmap_get(&val.inner.obj, "ee", (const void**)&out);
    NUTEST_ASSERT(found == 1);
    NUTEST_ASSERT(strcmp(out->inner.str, "ff") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_parse_obj_double(void) {
    stream st;
    const char* in = "{\"x\": \"y\", \"x\": \"z\"}";
    stream_open_memory(&st, in, strlen(in));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(val.ty == JSON_TY_OBJECT);
    NUTEST_ASSERT(val.inner.obj.count == 1);
    json_value* out;
    int found = hashmap_get(&val.inner.obj, "x", (const void**)&out);
    NUTEST_ASSERT(found == 1);
    NUTEST_ASSERT(strcmp(out->inner.str, "z") == 0);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result assert_json_value_copy(const char* str) {
    stream st;
    stream_open_memory(&st, str, strlen(str));
    json_value val;
    int err = json_parse(&st, &val);
    NUTEST_ASSERT(err == 0);
    json_value copy;
    json_value_copy(&copy, &val);
    NUTEST_ASSERT(val.ty == copy.ty);
    json_value_free(&copy);
    json_value_free(&val);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result json_value_copy_null(void) {
    return assert_json_value_copy("null");
}

nutest_result json_value_copy_true(void) {
    return assert_json_value_copy("true");
}

nutest_result json_value_copy_false(void) {
    return assert_json_value_copy("false");
}

nutest_result json_value_copy_number(void) {
    return assert_json_value_copy("42");
}

nutest_result json_value_copy_string(void) {
    return assert_json_value_copy("\"abcdef\"");
}

nutest_result json_value_copy_array(void) {
    return assert_json_value_copy("[45, \"abc\"]");
}

nutest_result json_value_copy_object(void) {
    return assert_json_value_copy("{\"abc\": true}");
}

int main() {
    nutest_register(json_parse_str_ascii);
    nutest_register(json_parse_str_long);
    nutest_register(json_parse_str_escape);
    nutest_register(json_parse_str_utf8_escape);
    nutest_register(json_parse_number_positive);
    nutest_register(json_parse_number_frac);
    nutest_register(json_parse_number_negative);
    nutest_register(json_parse_number_negative_frac);
    nutest_register(json_parse_number_zero);
    nutest_register(json_parse_number_negative_zero);
    nutest_register(json_parse_number_e);
    nutest_register(json_parse_number_leading_zero);
    nutest_register(json_parse_number_negative_leading_zero);
    nutest_register(json_parse_number_plus);
    nutest_register(json_parse_number_minus);
    nutest_register(json_parse_number_hex);
    nutest_register(json_parse_number_double_dot);
    nutest_register(json_parse_number_double_e);
    nutest_register(json_parse_array_empty);
    nutest_register(json_parse_array_numbers);
    nutest_register(json_parse_array_nested);
    nutest_register(json_parse_obj_empty);
    nutest_register(json_parse_obj_str_single);
    nutest_register(json_parse_obj_str_multi);
    nutest_register(json_parse_obj_double);
    nutest_register(json_value_copy_null);
    nutest_register(json_value_copy_true);
    nutest_register(json_value_copy_false);
    nutest_register(json_value_copy_number);
    nutest_register(json_value_copy_string);
    nutest_register(json_value_copy_array);
    nutest_register(json_value_copy_object);
    return nutest_run();
}
