#ifndef CGOTPL_ENCODE
#define CGOTPL_ENCODE

#include <stddef.h>
#include <stdint.h>

// out needs to be at least 4 bytes
void utf8_encode(int32_t cp, char* out, size_t* out_len);

#endif