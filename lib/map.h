#ifndef CGOTPL_MAP
#define CGOTPL_MAP

#include <stddef.h>

typedef struct {
    void* key;
    void* value;
} entry;

typedef struct {
    size_t cap;
    size_t len;
    entry* data;
} bucket;

typedef int hash_func;
#define HASH_FUNC_IDENTITY 1
#define HASH_FUNC_DJB2 2

typedef int (*hashmap_cmp)(const void*, const void*);
typedef size_t (*hashmap_key_len)(const void*);

typedef struct {
    bucket* data;
    size_t len;
    size_t count;
    hash_func hash;
    hashmap_cmp cmp;
    hashmap_key_len key_len;
} hashmap;

void hashmap_new(hashmap* map, hashmap_cmp cmp, hashmap_key_len key_len, hash_func hash);
void hashmap_free(hashmap* map);

// May return the previous entry stored.
// If entry.key is NULL there wasn't a previous entry.
entry hashmap_insert(hashmap* map, void* key, void* value);
int hashmap_get(const hashmap* map, const void* key, const void** out);
void hashmap_iter(const hashmap* map, void* userdata, void (*f)(entry*, void*));
void** hashmap_keys(const hashmap* map);

int hashmap_strcmp(const void* a, const void* b);
size_t hashmap_strlen(const void* a);

#endif
