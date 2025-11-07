#include "template.h"

#include <string.h>

#include "func.h"
#include "test.h"

nutest_result assert_eval_null(const char* tpl, const char* expected) {
    json_value val = JSON_NULL;
    char* out;
    int err = template_eval_mem(tpl, strlen(tpl), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(expected, out) == 0);
    free(out);
    json_value_free(&val);
    return NUTEST_PASS;
}

nutest_result assert_eval_err(const char* tpl, int expected) {
    json_value val = JSON_NULL;
    char* out;
    int err = template_eval_mem(tpl, strlen(tpl), &val, &out);
    NUTEST_ASSERT(err == expected);
    free(out);
    return NUTEST_PASS;
}

int make_json_val(json_value* val, const char* json) {
    stream st;
    stream_open_memory(&st, json, strlen(json));
    int err = json_parse(&st, val);
    stream_close(&st);
    return err;
}

nutest_result assert_eval_data(const char* tpl, const char* data, const char* expected) {
    json_value val;
    int err = make_json_val(&val, data);
    NUTEST_ASSERT(err == 0);
    char* out;
    err = template_eval_mem(tpl, strlen(tpl), &val, &out);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(strcmp(expected, out) == 0);
    free(out);
    json_value_free(&val);
    return NUTEST_PASS;
}

nutest_result assert_eval_err_data(const char* tpl, const char* data, int expected) {
    json_value val;
    int err = make_json_val(&val, data);
    NUTEST_ASSERT(err == 0);
    char* out;
    err = template_eval_mem(tpl, strlen(tpl), &val, &out);
    NUTEST_ASSERT(err == expected);
    free(out);
    json_value_free(&val);
    return NUTEST_PASS;
}

nutest_result template_identity(void) {
    return assert_eval_null(" abc{  def  ghi", " abc{  def  ghi");
}

nutest_result template_empty_pipeline(void) {
    return assert_eval_err(" x{{}} y", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_incomplete_pipeline(void) {
    return assert_eval_err("{{ 53 }}{{ ", ERR_TEMPLATE_UNEXPECTED_EOF);
}

nutest_result template_strip_whitespace_pre(void) {
    return assert_eval_null(" r      {{- ``}}s", " rs");
}

nutest_result template_strip_whitespace_post(void) {
    return assert_eval_null(" k {{`` -}}    ", " k ");
}

nutest_result template_strip_whitespace_multi(void) {
    return assert_eval_null("k {{- `` -}} {{- `` -}} l", "kl");
}

nutest_result template_strip_whitespace_pipeline(void) {
    return assert_eval_null("{{ 7 }} {{- `` }}", "7");
}

nutest_result template_strip_pre_no_inner_space(void) {
    return assert_eval_err(" {{-false}}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_comment_plain(void) {
    return assert_eval_null("2 {{/* this is a comment */}} 1", "2  1");
}

nutest_result template_comment_strip(void) {
    return assert_eval_null("2 {{- /* this is a comment */ -}} 1", "21");
}

nutest_result template_comment_pre_content(void) {
    return assert_eval_err("2 {{ 43 /* this is a comment */}} 1", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_comment_post_content(void) {
    return assert_eval_err("2 {{/* this is a comment */ 34}} 1", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_print_positive_number(void) {
    return assert_eval_null("b {{- 16 }}c", "b16c");
}

nutest_result template_print_negative_number(void) {
    return assert_eval_null("b {{- -24 }}c", "b-24c");
}

nutest_result template_print_regular_str(void) {
    return assert_eval_null("d {{- \"hello\" }}e", "dhelloe");
}

nutest_result template_print_backtick_str(void) {
    return assert_eval_null("f {{- `bye\"` }}g", "fbye\"g");
}

nutest_result template_print_list_elems(void) {
    return assert_eval_data("{{.}}", "[2,4,8]", "[2 4 8]");
}

nutest_result template_print_list_empty(void) {
    return assert_eval_data("{{.}}", "[]", "[]");
}

nutest_result template_print_obj_elem(void) {
    return assert_eval_data("{{.}}", "{\"a\":null}", "map[a:<nil>]");
}

nutest_result template_print_obj_elems(void) {
    return assert_eval_data("{{.}}", "{\"a\":null, \"b\": 45}", "map[a:<nil> b:45]");
}

nutest_result template_print_obj_empty(void) {
    return assert_eval_data("{{.}}", "{}", "map[]");
}

nutest_result template_func_unknown(void) {
    return assert_eval_err("xyz{{ banana }} h", ERR_TEMPLATE_FUNC_UNKNOWN);
}

nutest_result template_func_literal_bool(void) {
    return assert_eval_null("{{ true }} {{ false }}", "true false");
}

nutest_result template_func_literal_nil(void) {
    return assert_eval_err("{{ nil }}", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_end(void) {
    return assert_eval_err("{{ end }}", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_else(void) {
    return assert_eval_err("{{ else }}", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_break(void) {
    return assert_eval_err("{{ break }}", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_continue(void) {
    return assert_eval_err("{{ continue }}", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_if_no_arg(void) {
    return assert_eval_err("{{ if }}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_if_no_end(void) {
    return assert_eval_err("{{ if true -}} result", ERR_TEMPLATE_UNEXPECTED_EOF);
}

nutest_result template_if_true(void) {
    return assert_eval_null("{{ if true -}} chandra {{- end }}", "chandra");
}

nutest_result template_if_nested(void) {
    return assert_eval_null("{{ if true -}} {{ if true -}} nissa {{- end }} {{- end }}", "nissa");
}

nutest_result template_if_false(void) {
    return assert_eval_null("{{ if false -}} pear {{- end }}plume", "plume");
}

nutest_result template_if_noop(void) {
    return assert_eval_null("{{ if false -}} {{ if true }} paris {{ end }} {{- end }}london", "london");
}

nutest_result template_if_else_true(void) {
    return assert_eval_null("{{ if true -}} ttt {{- else -}} fff {{ end -}} ", "ttt");
}

nutest_result template_if_else_false(void) {
    return assert_eval_null("{{ if false -}} ttt {{- else -}} fff {{ end -}} ", "fff ");
}

nutest_result template_if_else_nested(void) {
    return assert_eval_null(
        "{{ if false -}} {{ if false -}} ttt {{- else -}} fff {{- end -}} {{- else -}} {{ if false -}} uuu {{- else -}} ggg {{- end -}} {{- end }}",
        "ggg");
}

nutest_result template_elseif_if(void) {
    return assert_eval_null("{{ if true -}} m {{- else if false -}} t {{ else }} g {{ end -}} ", "m");
}

nutest_result template_elseif_elif(void) {
    return assert_eval_null("{{ if false -}} m {{- else if true -}} t {{ else }} g {{ end -}} ", "t ");
}

nutest_result template_elseif_else(void) {
    return assert_eval_null("{{ if false -}} m {{- else if false -}} t {{ else }} g {{ end -}} ", " g ");
}

nutest_result template_elseif_multi(void) {
    return assert_eval_null("{{ if false -}} a {{- else if false -}} b {{- else if true -}} c {{ else }} d {{ end -}} ", "c ");
}

nutest_result template_elseif_syntax(void) {
    return assert_eval_err("{{ if false -}} m {{- else blub true -}} t {{ else }} g {{ end -}} ", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_if_double_else(void) {
    return assert_eval_err("{{if true}}a{{else}}b{{else}}c{{end}}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_dot_expr_root(void) {
    return assert_eval_data("{{.}}", "77", "77");
}

nutest_result template_dot_expr_path(void) {
    return assert_eval_data("{{.c}}", "{\"c\":\"++\"}", "++");
}

nutest_result template_path_expr(void) {
    return assert_eval_data("{{ .left }}", "{\"left\": \"right\"}", "right");
}

nutest_result template_path_expr_invalid_syntax(void) {
    return assert_eval_err_data("{{ .left. }}", "{\"left\": \"right\"}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_path_expr_no_object(void) {
    return assert_eval_err("{{ .down }}", ERR_TEMPLATE_NO_OBJECT);
}

nutest_result template_path_expr_single_key_unknown(void) {
    return assert_eval_data("{{ .up }}", "{\"left\": \"right\"}", "<no value>");
}

nutest_result template_path_expr_multi_key_unknown(void) {
    return assert_eval_data("{{ .up.down }}", "{\"left\": \"right\"}", "<no value>");
}

nutest_result template_range_int_positive(void) {
    return assert_eval_null("{{ range 5 }}{{.}}{{ end }}", "01234");
}

nutest_result template_range_int_negative(void) {
    return assert_eval_null("{{ range -6 }}{{.}}{{ end -}} g", "g");
}

nutest_result template_range_double_positive(void) {
    return assert_eval_err("{{ range 5.1 }}{{.}}{{ end }}", ERR_TEMPLATE_NO_ITERABLE);
}

nutest_result template_range_double_negative(void) {
    return assert_eval_err("{{ range -1.5 }}{{.}}{{ end }}", ERR_TEMPLATE_NO_ITERABLE);
}

nutest_result template_range_simple(void) {
    return assert_eval_data("{{ range .list }}{{ . }}{{end}}", "{\"list\": [1,2,3]}", "123");
}

nutest_result template_range_no_arg(void) {
    return assert_eval_err("{{ range }} . {{ end }}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_range_nested(void) {
    return assert_eval_data("{{ range . }} {{ range . -}} {{ . }} {{- end }} {{- end }}", "[[\"a\",\"b\"],[\"c\",\"d\"]]", " ab cd");
}

nutest_result template_range_empty_nested(void) {
    return assert_eval_data("x {{- range . }}{{ range . -}} yz {{- end }}{{ end }}", "[[]]", "x");
}

nutest_result template_range_else(void) {
    return assert_eval_data("{{ range . }} cc {{ else }} dd {{ end }}", "[9]", " cc ");
}

nutest_result template_range_empty_else(void) {
    return assert_eval_data("{{ range . }} a {{ else -}} b {{- end }}", "[]", "b");
}

nutest_result template_range_double_else(void) {
    return assert_eval_err_data("{{range .}}1{{else}}2{{else}}3{{end}}", "[false]", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_range_obj(void) {
    return assert_eval_data("{{ range .obj }}{{ . }}{{end}}", "{\"obj\": {\"a\": false}}", "false");
}

nutest_result template_range_obj_many_keys(void) {
    return assert_eval_data("{{ range .obj }}{{ . }}{{end}}", "{\"obj\": {\"a\": 9, \"b\": 8, \"c\": 7, \"def\": 6}}", "9876");
}

nutest_result template_range_obj_complex(void) {
    return assert_eval_err_data("{{range .}} {{else}} {{else}.", "{\"a\": 28, \"b\": [true], \"d\": 43.2}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_range_break(void) {
    return assert_eval_data("{{range .}}a{{if .}}{{break}}{{end}}{{end}}", "[false, false, true, false]", "aaa");
}

nutest_result template_range_continue(void) {
    return assert_eval_data("{{range .}}z{{if .}}{{continue}}{{end}}a{{end}}", "[false, false, true, false]", "zazazza");
}

nutest_result template_with_obj(void) {
    return assert_eval_data("{{ with .a }} {{.}} {{end}}", "{\"a\": 1227}", " 1227 ");
}

nutest_result template_with_unknown_key(void) {
    return assert_eval_data("{{ with .b }} xyz {{ end }}", "{}", "");
}

nutest_result template_with_obj_else(void) {
    return assert_eval_data("{{ with .a }} {{.}} {{ else }}fgh{{ end }}", "{\"a\": []}", "fgh");
}

nutest_result template_with_obj_else_with(void) {
    return assert_eval_data("{{ with .a }} {{.}} {{else with .b}}{{.}}{{ else }}fgh{{ end }}", "{\"a\": [], \"b\": 2724}", "2724");
}

nutest_result template_with_override_scratch(void) {
    return assert_eval_null("{{ with `a` }} {{ 1 }} {{ . }} {{ end }}", " 1 a ");
}

nutest_result template_with_no_arg(void) {
    return assert_eval_err("{{ with }} a {{end}}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_with_double_else(void) {
    return assert_eval_err("{{ with . }} a {{ else }} b {{ else }} c {{ end }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_var_dollar(void) {
    return assert_eval_data("{{ $ }}", "529", "529");
}

nutest_result template_var_unknown(void) {
    return assert_eval_err("{{ $unknown }}", ERR_TEMPLATE_VAR_UNKNOWN);
}

nutest_result template_var_define(void) {
    return assert_eval_null("{{ $a := \"321\" }}{{ $a }}", "321");
}

nutest_result template_var_define_nested(void) {
    return assert_eval_err("{{$x := $y := 7331}}", ERR_TEMPLATE_VAR_UNKNOWN);
}

nutest_result template_var_define_nil(void) {
    return assert_eval_err("{{$var:=nil}}abc", ERR_TEMPLATE_KEYWORD_UNEXPECTED);
}

nutest_result template_var_define_within_if(void) {
    return assert_eval_null("{{if $var:=345}}success{{end}}", "success");
}

nutest_result template_var_define_within_with(void) {
    return assert_eval_null("{{with $var:=678}}yay{{end}}", "yay");
}

nutest_result template_var_assign_undefined(void) {
    return assert_eval_err("{{$undefined=`pppp`}}", ERR_TEMPLATE_VAR_UNKNOWN);
}

nutest_result template_var_redefine(void) {
    return assert_eval_null("{{$x := 7}}{{$x := 8}}{{$x}}", "8");
}

nutest_result template_var_scope_kept(void) {
    return assert_eval_null("{{$z := true}}{{with 137}}text {{end}}{{$z}}", "text true");
}

nutest_result template_var_scope_lost(void) {
    return assert_eval_err("{{ with $ }}{{$y := 3}}{{end}}{{$y}}", ERR_TEMPLATE_VAR_UNKNOWN);
}

nutest_result template_loop_var_value(void) {
    return assert_eval_data("{{ range $val := . -}} {{ $val }} {{- end }}", "[9,8,7]", "987");
}

nutest_result template_loop_var_arr(void) {
    return assert_eval_data("{{ range $idx,$val := . -}} {{ $idx }}{{ $val }} {{- end }}", "[\"a\",\"b\"]", "0a1b");
}

nutest_result template_loop_var_map(void) {
    return assert_eval_data("{{ range $key,$val := . -}} {{ $key }}{{ $val }} {{- end }}", "{\"a\": 9, \"b\": 8}", "a9b8");
}

nutest_result template_func_not_true(void) {
    return assert_eval_null("{{ not true }}", "false");
}

nutest_result template_func_not_false(void) {
    return assert_eval_null("{{ not false }}", "true");
}

nutest_result template_func_not_no_args(void) {
    return assert_eval_err("{{ not }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_func_not_many_args(void) {
    return assert_eval_err("{{ not true false `hi` }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_func_and(void) {
    return assert_eval_null("{{ and 0 0 }}{{ and 1 0 }}{{ and 0 1 }}{{ and 1 1 }}", "0001");
}

nutest_result template_func_or(void) {
    return assert_eval_null("{{ or 0 0 }}{{ or 1 0 }}{{ or 0 1 }}{{ or 1 1 }}", "0111");
}

nutest_result template_func_len_str(void) {
    return assert_eval_null("{{ len `abc` }}", "3");
}

nutest_result template_func_len_arr(void) {
    return assert_eval_data("{{ len . }}", "[1, 2]", "2");
}

nutest_result template_func_len_obj(void) {
    return assert_eval_data("{{ len . }}", "{\"x\":3, \"y\": []}", "2");
}

nutest_result template_func_len_num(void) {
    return assert_eval_err("{{ len 56 }}", ERR_FUNC_INVALID_ARG_TYPE);
}

nutest_result template_func_print(void) {
    return assert_eval_data("{{ print \"a\" .x . true false 7 }}", "{\"x\": [1,2]}", "a[1 2] map[x:[1 2]] true false 7");
}

nutest_result template_func_println(void) {
    return assert_eval_null("{{ println 321 }}", "321\n");
}

nutest_result template_func_index_no_arg(void) {
    return assert_eval_err("{{ index }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_func_index_single_arg(void) {
    return assert_eval_null("{{ index 7 }}", "7");
}

nutest_result template_func_index_invalid_type(void) {
    return assert_eval_err("{{ index 7 true }}", ERR_FUNC_INVALID_ARG_TYPE);
}

nutest_result template_func_index_simple_obj(void) {
    return assert_eval_data("{{ index . \"z\" }}", "{\"z\":\"y\"}", "y");
}

nutest_result template_func_index_nested_obj(void) {
    return assert_eval_data("{{ index . \"z\" \"y\" }}", "{\"z\": {\"y\": 34}}", "34");
}

nutest_result template_func_index_simple_arr(void) {
    return assert_eval_data("{{ index . 1 }}", "[4,5,6]", "5");
}

nutest_result template_func_index_nested_arr(void) {
    return assert_eval_data("{{ index . 1 0 }}", "[[1,2], [3,4]]", "3");
}

nutest_result template_func_index_mixed(void) {
    return assert_eval_data("{{ index . \"z\" 2 }}", "{\"z\":[3,2,false]}", "false");
}

nutest_result template_func_if_arg(void) {
    return assert_eval_null("{{ if not false -}} yes {{- end }}", "yes");
}

nutest_result template_func_with_arg(void) {
    return assert_eval_null("{{ with not false }}{{ . }}{{ end }}", "true");
}

nutest_result template_func_arg_invalid_name(void) {
    return assert_eval_err("{{ not $# }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_parenthesis_val(void) {
    return assert_eval_null("{{ (45) }}", "45");
}

nutest_result template_parenthesis_no_val(void) {
    return assert_eval_err("{{ () }}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_parenthesis_no_close(void) {
    return assert_eval_err("{{ ( true }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_parenthesis_no_open(void) {
    return assert_eval_err("{{ false ) }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_parenthesis_func(void) {
    return assert_eval_null("{{ not (not ``) }}", "false");
}

nutest_result template_pipe_plain(void) {
    return assert_eval_null("{{ `a` | not }}", "false");
}

nutest_result template_pipe_multi(void) {
    return assert_eval_null("{{ true | not | not | not | not }}", "true");
}

nutest_result template_pipe_parenthesis(void) {
    return assert_eval_null("{{ ( false | not ) }}", "true");
}

nutest_result template_pipe_no_func(void) {
    return assert_eval_err("{{ 7 | nofunc }}", ERR_TEMPLATE_FUNC_UNKNOWN);
}

nutest_result template_pipe_invalid_args(void) {
    return assert_eval_err("{{ 7 | not true }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_pipe_var_def(void) {
    return assert_eval_null("{{ $a := true | not }}{{ $a }}", "false");
}

nutest_result template_pipe_piped_var(void) {
    return assert_eval_null("{{ ($a := true) | not }}{{ $a }}", "falsetrue");
}

nutest_result template_loop_null_name(void) {
    json_value val;
    int err = make_json_val(&val, "[3, 2, 1]");
    NUTEST_ASSERT(err == 0);
    const char tpl[] = "{{ range $abc\0def := . -}} {{ $abc\0def }} {{- end }}";
    char* out;
    err = template_eval_mem(tpl, sizeof(tpl), &val, &out);
    NUTEST_ASSERT(err == ERR_TEMPLATE_INVALID_SYNTAX);
    free(out);
    json_value_free(&val);
    return NUTEST_PASS;
}

nutest_result template_define_invoke(void) {
    return assert_eval_null("{{ define `x` -}} kek {{- end }}{{ template `x` . }}e", "keke");
}

nutest_result template_define_change_dot(void) {
    return assert_eval_null("{{ define `y` }}{{.}}{{end}}{{ template `y` 8 }}", "8");
}

nutest_result template_define_no_name(void) {
    return assert_eval_err("{{ define }}abc{{ end }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_define_nested(void) {
    return assert_eval_err("{{ define `a` }}{{ define `b` }}c{{ end }}{{ end }}{{ template `a` `` }}", ERR_TEMPLATE_DEFINE_NESTED);
}

nutest_result template_template_no_val(void) {
    return assert_eval_err("{{ define `cba` }}zyx{{ end }}{{ template `cba` }}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_template_unknown(void) {
    return assert_eval_err("{{ template `unknown` . }}", ERR_TEMPLATE_DEFINE_UNKNOWN);
}

nutest_result template_block_invoke(void) {
    return assert_eval_null("{{ block `x` . -}} lol {{- end }}{{ template `x` . }}o", "lollolo");
}

nutest_result template_block_change_dot(void) {
    return assert_eval_null("{{ block `y` 9 }}{{.}}{{end}}", "9");
}

nutest_result template_block_no_name(void) {
    return assert_eval_err("{{ block }}abc{{ end }}", ERR_TEMPLATE_INVALID_SYNTAX);
}

nutest_result template_block_no_val(void) {
    return assert_eval_err("{{ block `alpha` }}abc{{ end }}", ERR_TEMPLATE_NO_VALUE);
}

nutest_result template_block_non_executed(void) {
    return assert_eval_null("{{ if false }}{{ block `x` . -}} lol {{- end }}{{ end }}{{ template `x` . }}o", "lolo");
}

nutest_result template_slice_str_single_idx(void) {
    return assert_eval_null("{{ slice `zyx` 1 }}", "yx");
}

nutest_result template_slice_str_two_idx(void) {
    return assert_eval_null("{{ slice `zyx` 1 2 }}", "y");
}

nutest_result template_slice_str_three_idx(void) {
    return assert_eval_err("{{ slice `zyx` 1 2 3 }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_slice_arr_single_idx(void) {
    return assert_eval_data("{{ slice . 2 }}", "[1,2,3,4]", "[3 4]");
}

nutest_result template_slice_arr_two_idx(void) {
    return assert_eval_data("{{ slice . 2 3 }}", "[1,2,3,4]", "[3]");
}

nutest_result template_slice_arr_three_idx(void) {
    return assert_eval_data("{{ slice . 2 3 4 }}", "[1,2,3,4]", "[3]");
}

nutest_result template_slice_single_arg(void) {
    return assert_eval_err("{{ slice `hi` }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_slice_many_args(void) {
    return assert_eval_err("{{ slice `hi` 0 1 2 3 4 5 6 }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_slice_negative_start(void) {
    return assert_eval_err("{{ slice `hello` -3 }}", ERR_FUNC_INVALID_ARG_VAL);
}

nutest_result template_slice_negative_end(void) {
    return assert_eval_err("{{ slice `hello` 2 -5 }}", ERR_FUNC_INVALID_ARG_VAL);
}

nutest_result template_slice_huge_start(void) {
    return assert_eval_err("{{ slice `hello` 3333 }}", ERR_FUNC_INVALID_ARG_VAL);
}

nutest_result template_slice_huge_end(void) {
    return assert_eval_err("{{ slice `hello` 2 555 }}", ERR_FUNC_INVALID_ARG_VAL);
}

nutest_result template_slice_start_greater_end(void) {
    return assert_eval_err("{{ slice `hello` 4 2 }}", ERR_FUNC_INVALID_ARG_VAL);
}

nutest_result template_eq_single_num(void) {
    return assert_eval_null("{{ eq 4 7 }}", "false");
}

nutest_result template_eq_many_num(void) {
    return assert_eval_null("{{ eq 4 7 3 4 }}", "true");
}

nutest_result template_eq_single_str(void) {
    return assert_eval_null("{{ eq `f` `h` }}", "false");
}

nutest_result template_eq_true_false(void) {
    return assert_eval_null("{{ eq true false }}", "false");
}

nutest_result template_eq_num_str(void) {
    return assert_eval_err("{{ eq 6 `6` }}", ERR_FUNC_INVALID_ARG_TYPE);
}

nutest_result template_eq_arr_nil(void) {
    return assert_eval_data("{{ eq . nil }}", "[1, 2]", "false");
}

nutest_result template_eq_obj_nil(void) {
    return assert_eval_data("{{ eq . nil }}", "{}", "false");
}

nutest_result template_eq_nil_nil(void) {
    return assert_eval_null("{{ eq nil nil }}", "true");
}

nutest_result template_eq_no_arg(void) {
    return assert_eval_err("{{ eq }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_eq_single_arg(void) {
    return assert_eval_err("{{ eq `z` }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_ne_true_false(void) {
    return assert_eval_null("{{ ne true false }}", "true");
}

nutest_result template_ne_true_true(void) {
    return assert_eval_null("{{ ne true true }}", "false");
}

nutest_result template_ne_no_arg(void) {
    return assert_eval_err("{{ ne }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_ne_single_arg(void) {
    return assert_eval_err("{{ ne `z` }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_ne_many_arg(void) {
    return assert_eval_err("{{ ne `z` 1 2 3 }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_gt_num(void) {
    return assert_eval_null("{{ gt 3 2 }} {{ gt 2 2 }} {{ gt 1 2 }}", "true false false");
}

nutest_result template_ge_num(void) {
    return assert_eval_null("{{ ge 3 2 }} {{ ge 2 2 }} {{ ge 1 2 }}", "true true false");
}

nutest_result template_lt_num(void) {
    return assert_eval_null("{{ lt 3 2 }} {{ lt 2 2 }} {{ lt 1 2 }}", "false false true");
}

nutest_result template_le_num(void) {
    return assert_eval_null("{{ le 3 2 }} {{ le 2 2 }} {{ le 1 2 }}", "false true true");
}

nutest_result template_gt_str(void) {
    return assert_eval_null("{{ gt `z` `y` }} {{ gt `y` `y` }} {{ gt `x` `y` }}", "true false false");
}

nutest_result template_lt_str(void) {
    return assert_eval_null("{{ lt `z` `y` }} {{ lt `y` `y` }} {{ lt `x` `y` }}", "false false true");
}

nutest_result template_ge_str(void) {
    return assert_eval_null("{{ ge `z` `y` }} {{ ge `y` `y` }} {{ ge `x` `y` }}", "true true false");
}

nutest_result template_le_str(void) {
    return assert_eval_null("{{ le `z` `y` }} {{ le `y` `y` }} {{ le `x` `y` }}", "false true true");
}

nutest_result template_gt_no_arg(void) {
    return assert_eval_err("{{ gt }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_gt_single_arg(void) {
    return assert_eval_err("{{ gt 3 }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_gt_many_arg(void) {
    return assert_eval_err("{{ gt 3 4 5 }}", ERR_FUNC_INVALID_ARG_LEN);
}

nutest_result template_gt_mismatch_ty(void) {
    return assert_eval_err("{{ gt `v` 5 }}", ERR_FUNC_INVALID_ARG_TYPE);
}

nutest_result template_urlquery_str(void) {
    return assert_eval_null("{{ urlquery `a` `€` }}", "a%E2%82%AC");
}

nutest_result template_urlquery_nil(void) {
    return assert_eval_null("{{ urlquery nil }}", "%3Cno+value%3E");
}

nutest_result template_html_str(void) {
    return assert_eval_null("{{ html `<>&'\"` `abc` }}", "&lt;&gt;&amp;&#39;&#34;abc");
}

nutest_result template_html_nil(void) {
    return assert_eval_null("{{ html nil }}", "&lt;no value&gt;");
}

nutest_result template_js_str(void) {
    return assert_eval_null("{{ js `<>&'\"` `ABC` }}", "\\u003C\\u003E\\u0026\\'\\\"ABC");
}

nutest_result template_js_nil(void) {
    return assert_eval_null("{{ js nil }}", "\\u003Cno value\\u003E");
}

nutest_result template_js_utf8_roundtrip(void) {
    return assert_eval_null("{{ js \"\\u00A0\" }}", "\\u00A0");
}

nutest_result template_printf_e(void) {
    return assert_eval_null("{{ printf `%e %E` 2000 3000 }}", "2.000000e+03 3.000000E+03");
}

nutest_result template_printf_f(void) {
    return assert_eval_null("{{ printf `%f %F` 157.5 751.125 }}", "157.500000 751.125000");
}

nutest_result template_printf_g(void) {
    return assert_eval_null("{{ printf `%g` 623.75 }}", "623.75");
}

nutest_result template_printf_q(void) {
    return assert_eval_null("{{ printf `%q` `a\"b` }}", "\"a\\\"b\"");
}

nutest_result template_printf_s(void) {
    return assert_eval_null("{{ printf `%s%s%s` `a` `c` `b` }}", "acb");
}

nutest_result template_printf_t(void) {
    return assert_eval_null("{{ printf `%t%t` false true }}", "falsetrue");
}

nutest_result template_printf_v(void) {
    return assert_eval_data("{{ printf `%v` . }}", "[2,4,8]", "[2 4 8]");
}

nutest_result template_printf_x(void) {
    return assert_eval_null("{{ printf `%x %X` 8.5 16.75 }}", "0x1.1p+3 0X1.0CP+4");
}

nutest_result template_printf_x_str(void) {
    return assert_eval_null("{{ printf `%x %X` `z` `z` }}", "7a 7A");
}

nutest_result template_printf_missing(void) {
    return assert_eval_null("{{ printf `%s` }}", "%!s(MISSING)");
}

nutest_result template_printf_complex(void) {
    return assert_eval_data("{{ printf `%s` . }}", "[{\"a\":3.5}, {\"b\":true}, [false, null]]", "[map[a:%!s(float64=3.5)] map[b:%!s(bool=true)] [%!s(bool=false) <nil>]]");
}

int main() {
    nutest_register(template_identity);
    nutest_register(template_empty_pipeline);
    nutest_register(template_incomplete_pipeline);
    nutest_register(template_strip_whitespace_pre);
    nutest_register(template_strip_whitespace_post);
    nutest_register(template_strip_whitespace_multi);
    nutest_register(template_strip_whitespace_pipeline);
    nutest_register(template_strip_pre_no_inner_space);
    nutest_register(template_comment_plain);
    nutest_register(template_comment_strip);
    nutest_register(template_comment_pre_content);
    nutest_register(template_comment_post_content);
    nutest_register(template_print_positive_number);
    nutest_register(template_print_negative_number);
    nutest_register(template_print_regular_str);
    nutest_register(template_print_backtick_str);
    nutest_register(template_print_list_elems);
    nutest_register(template_print_list_empty);
    nutest_register(template_print_obj_elem);
    nutest_register(template_print_obj_elems);
    nutest_register(template_print_obj_empty);
    nutest_register(template_func_unknown);
    nutest_register(template_func_literal_bool);
    nutest_register(template_func_literal_nil);
    nutest_register(template_end);
    nutest_register(template_else);
    nutest_register(template_break);
    nutest_register(template_continue);
    nutest_register(template_if_no_arg);
    nutest_register(template_if_no_end);
    nutest_register(template_if_true);
    nutest_register(template_if_nested);
    nutest_register(template_if_false);
    nutest_register(template_if_noop);
    nutest_register(template_if_else_true);
    nutest_register(template_if_else_false);
    nutest_register(template_if_else_nested);
    nutest_register(template_elseif_if);
    nutest_register(template_elseif_elif);
    nutest_register(template_elseif_else);
    nutest_register(template_elseif_multi);
    nutest_register(template_elseif_syntax);
    nutest_register(template_if_double_else);
    nutest_register(template_dot_expr_root);
    nutest_register(template_dot_expr_path);
    nutest_register(template_path_expr);
    nutest_register(template_path_expr_invalid_syntax);
    nutest_register(template_path_expr_no_object);
    nutest_register(template_path_expr_single_key_unknown);
    nutest_register(template_path_expr_multi_key_unknown);
    nutest_register(template_range_int_positive);
    nutest_register(template_range_int_negative);
    nutest_register(template_range_double_positive);
    nutest_register(template_range_double_negative);
    nutest_register(template_range_simple);
    nutest_register(template_range_no_arg);
    nutest_register(template_range_nested);
    nutest_register(template_range_empty_nested);
    nutest_register(template_range_else);
    nutest_register(template_range_empty_else);
    nutest_register(template_range_double_else);
    nutest_register(template_range_obj);
    nutest_register(template_range_obj_many_keys);
    nutest_register(template_range_obj_complex);
    nutest_register(template_range_break);
    nutest_register(template_range_continue);
    nutest_register(template_with_obj);
    nutest_register(template_with_unknown_key);
    nutest_register(template_with_obj_else);
    nutest_register(template_with_obj_else_with);
    nutest_register(template_with_override_scratch);
    nutest_register(template_with_no_arg);
    nutest_register(template_with_double_else);
    nutest_register(template_var_dollar);
    nutest_register(template_var_unknown);
    nutest_register(template_var_define);
    nutest_register(template_var_define_nested);
    nutest_register(template_var_define_nil);
    nutest_register(template_var_define_within_if);
    nutest_register(template_var_define_within_with);
    nutest_register(template_var_assign_undefined);
    nutest_register(template_var_redefine);
    nutest_register(template_var_scope_kept);
    nutest_register(template_var_scope_lost);
    nutest_register(template_loop_var_value);
    nutest_register(template_loop_var_arr);
    nutest_register(template_loop_var_map);
    nutest_register(template_loop_null_name);
    nutest_register(template_func_not_true);
    nutest_register(template_func_not_false);
    nutest_register(template_func_not_no_args);
    nutest_register(template_func_not_many_args);
    nutest_register(template_func_and);
    nutest_register(template_func_or);
    nutest_register(template_func_len_str);
    nutest_register(template_func_len_obj);
    nutest_register(template_func_len_arr);
    nutest_register(template_func_len_num);
    nutest_register(template_func_print);
    nutest_register(template_func_println);
    nutest_register(template_func_index_no_arg);
    nutest_register(template_func_index_single_arg);
    nutest_register(template_func_index_invalid_type);
    nutest_register(template_func_index_simple_obj);
    nutest_register(template_func_index_nested_obj);
    nutest_register(template_func_index_simple_arr);
    nutest_register(template_func_index_nested_arr);
    nutest_register(template_func_index_mixed);
    nutest_register(template_func_if_arg);
    nutest_register(template_func_with_arg);
    nutest_register(template_func_arg_invalid_name);
    nutest_register(template_parenthesis_val);
    nutest_register(template_parenthesis_no_val);
    nutest_register(template_parenthesis_no_close);
    nutest_register(template_parenthesis_no_open);
    nutest_register(template_parenthesis_func);
    nutest_register(template_pipe_plain);
    nutest_register(template_pipe_multi);
    nutest_register(template_pipe_parenthesis);
    nutest_register(template_pipe_no_func);
    nutest_register(template_pipe_invalid_args);
    nutest_register(template_pipe_var_def);
    nutest_register(template_pipe_piped_var);
    nutest_register(template_define_invoke);
    nutest_register(template_define_change_dot);
    nutest_register(template_define_no_name);
    nutest_register(template_define_nested);
    nutest_register(template_template_no_val);
    nutest_register(template_template_unknown);
    nutest_register(template_block_invoke);
    nutest_register(template_block_change_dot);
    nutest_register(template_block_no_name);
    nutest_register(template_block_no_val);
    nutest_register(template_block_non_executed);
    nutest_register(template_slice_str_single_idx);
    nutest_register(template_slice_str_two_idx);
    nutest_register(template_slice_str_three_idx);
    nutest_register(template_slice_arr_single_idx);
    nutest_register(template_slice_arr_two_idx);
    nutest_register(template_slice_arr_three_idx);
    nutest_register(template_slice_single_arg);
    nutest_register(template_slice_many_args);
    nutest_register(template_slice_negative_start);
    nutest_register(template_slice_negative_end);
    nutest_register(template_slice_huge_start);
    nutest_register(template_slice_huge_end);
    nutest_register(template_slice_start_greater_end);
    nutest_register(template_eq_single_num);
    nutest_register(template_eq_many_num);
    nutest_register(template_eq_single_str);
    nutest_register(template_eq_true_false);
    nutest_register(template_eq_num_str);
    nutest_register(template_eq_arr_nil);
    nutest_register(template_eq_obj_nil);
    nutest_register(template_eq_nil_nil);
    nutest_register(template_eq_no_arg);
    nutest_register(template_eq_single_arg);
    nutest_register(template_ne_true_false);
    nutest_register(template_ne_true_true);
    nutest_register(template_ne_no_arg);
    nutest_register(template_ne_single_arg);
    nutest_register(template_ne_many_arg);
    nutest_register(template_gt_num);
    nutest_register(template_ge_num);
    nutest_register(template_lt_num);
    nutest_register(template_le_num);
    nutest_register(template_gt_str);
    nutest_register(template_ge_str);
    nutest_register(template_lt_str);
    nutest_register(template_le_str);
    nutest_register(template_gt_no_arg);
    nutest_register(template_gt_single_arg);
    nutest_register(template_gt_many_arg);
    nutest_register(template_gt_mismatch_ty);
    nutest_register(template_urlquery_str);
    nutest_register(template_urlquery_nil);
    nutest_register(template_html_str);
    nutest_register(template_html_nil);
    nutest_register(template_js_str);
    nutest_register(template_js_nil);
    nutest_register(template_js_utf8_roundtrip);
    nutest_register(template_printf_e);
    nutest_register(template_printf_f);
    nutest_register(template_printf_g);
    nutest_register(template_printf_q);
    nutest_register(template_printf_s);
    nutest_register(template_printf_t);
    nutest_register(template_printf_v);
    nutest_register(template_printf_x);
    nutest_register(template_printf_x_str);
    nutest_register(template_printf_missing);
    nutest_register(template_printf_complex);
    return nutest_run();
}
