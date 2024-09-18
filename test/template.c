#include "template.h"
#include <stdio.h>
#include <string.h>

#include "json.h"
#include "test.h"

nutest_result template_identity(void) {
    json_value val = JSON_NULL;
    const char* in = " abc{  def  ghi";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(in, out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_empty_pipeline(void) {
    json_value val = JSON_NULL;
    const char* in = " x{{}} y";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(" x y", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_strip_whitespace_pre(void) {
    json_value val = JSON_NULL;
    const char* in = " r      {{- }}s";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(" rs", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_strip_whitespace_post(void) {
    json_value val = JSON_NULL;
    const char* in = " k {{ -}}    ";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(" k ", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_print_number(void) {
    json_value val = JSON_NULL;
    const char* in = "b {{- 16 }}c";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    printf("err: %d\n", err);
    NUTEST_ASSERT(err == 0);
    printf("out: %s\n", out);
    NUTEST_ASSERT(strcmp("b16c", out) == 0);
    free(out);
    return NUTEST_PASS;
}

int main() {
    nutest_register(template_identity);
    nutest_register(template_empty_pipeline);
    nutest_register(template_strip_whitespace_pre);
    nutest_register(template_strip_whitespace_post);
    nutest_register(template_print_number);
    return nutest_run();
}
