#include "template.h"

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

nutest_result template_strip_whitespace_multi(void) {
    json_value val = JSON_NULL;
    const char* in = "k {{- -}} {{- -}} l";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("kl", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_print_positive_number(void) {
    json_value val = JSON_NULL;
    const char* in = "b {{- 16 }}c";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("b16c", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_print_negative_number(void) {
    json_value val = JSON_NULL;
    const char* in = "b {{- -24 }}c";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("b-24c", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_print_regular_str(void) {
    json_value val = JSON_NULL;
    const char* in = "d {{- \"hello\" }}e";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("dhelloe", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_print_backtick_str(void) {
    json_value val = JSON_NULL;
    const char* in = "f {{- `bye\"` }}g";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("fbye\"g", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_func_unknown(void) {
    json_value val = JSON_NULL;
    const char* in = "xyz{{ banana }} h";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == ERR_TEMPLATE_FUNC_UNKNOWN);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_func_literal(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ true }} {{ false }} {{ nil }}";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("true false ", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_end(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ end }}";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == ERR_TEMPLATE_KEYWORD_END);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_if_no_arg(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ if }}";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == ERR_TEMPLATE_NO_LITERAL);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_if_true(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ if true -}} chandra {{- end }}";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("chandra", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_if_true_nested(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ if true -}} {{ if true -}} nissa {{- end }} {{- end }}";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("nissa", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_if_false(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ if false -}} pear {{- end }}plume";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("plume", out) == 0);
    free(out);
    return NUTEST_PASS;
}

nutest_result template_if_nested(void) {
    json_value val = JSON_NULL;
    const char* in = "{{ if false -}} {{ if true }} paris {{ end }} {{- end }}london";
    char* out;
    int err = template_eval(in, strlen(in), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp("london", out) == 0);
    free(out);
    return NUTEST_PASS;
}

int main() {
    nutest_register(template_identity);
    nutest_register(template_empty_pipeline);
    nutest_register(template_strip_whitespace_pre);
    nutest_register(template_strip_whitespace_post);
    nutest_register(template_strip_whitespace_multi);
    nutest_register(template_print_positive_number);
    nutest_register(template_print_negative_number);
    nutest_register(template_print_regular_str);
    nutest_register(template_print_backtick_str);
    nutest_register(template_func_unknown);
    nutest_register(template_func_literal);
    nutest_register(template_end);
    nutest_register(template_if_no_arg);
    nutest_register(template_if_true);
    nutest_register(template_if_true_nested);
    nutest_register(template_if_false);
    nutest_register(template_if_nested);
    return nutest_run();
}
