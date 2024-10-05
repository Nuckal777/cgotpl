#ifndef CGOTPL_TEMPLATE
#define CGOTPL_TEMPLATE

#include <stddef.h>

#include "json.h"

#define ERR_TEMPLATE_INVALID_ESCAPE -900
#define ERR_TEMPLATE_INVALID_SYNTAX -901
#define ERR_TEMPLATE_BUFFER_OVERFLOW -902
#define ERR_TEMPLATE_FUNC_UNKNOWN -903
#define ERR_TEMPLATE_LITERAL_DASH -904
#define ERR_TEMPLATE_NO_LITERAL -905

int template_eval(const char* tpl, size_t n, json_value* dot, char** out);

#endif
