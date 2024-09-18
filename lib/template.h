#ifndef CGOTPL_TEMPLATE
#define CGOTPL_TEMPLATE

#include <stddef.h>

#include "json.h"

int template_eval(const char* tpl, size_t n, json_value* dot, char** out);

#endif
