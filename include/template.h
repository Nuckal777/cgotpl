#ifndef CGOTPL_TEMPLATE
#define CGOTPL_TEMPLATE

#include <stddef.h>

#include "json.h"

#define ERR_TEMPLATE_INVALID_ESCAPE -900
#define ERR_TEMPLATE_INVALID_SYNTAX -901
#define ERR_TEMPLATE_BUFFER_OVERFLOW -902
#define ERR_TEMPLATE_KEYWORD_UNKNOWN -903
#define ERR_TEMPLATE_FUNC_UNKNOWN -904
#define ERR_TEMPLATE_FUNC_INVALID -905
#define ERR_TEMPLATE_NO_VALUE -906
#define ERR_TEMPLATE_KEYWORD_UNEXPECTED -907
#define ERR_TEMPLATE_NO_OBJECT -908
#define ERR_TEMPLATE_NO_ITERABLE -909
#define ERR_TEMPLATE_KEY_UNKNOWN -910
#define ERR_TEMPLATE_VAR_UNKNOWN -911
#define ERR_TEMPLATE_NO_MUTATION -912
#define ERR_TEMPLATE_UNEXPECTED_EOF -913

// in is a pointer to a stream, which may be read to the end. dot is
// the inital dot value. out will be filled with the result of templating
// and needs to be freed by the caller. Returns 0 on success.
int template_eval_stream(stream* in, json_value* dot, char** out);

// tpl is a pointer to a template string from which up to n bytes are read.
// dot is the inital dot value. out will be filled with the result of
// templating and needs to be freed by the caller. Returns 0 on success.
int template_eval_mem(const char* tpl, size_t n, json_value* dot, char** out);

#endif
