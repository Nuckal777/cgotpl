#include "map.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUCKET_DEFAULT_CAP 4

void bucket_new(bucket* b) {
    b->len = 0;
    b->cap = BUCKET_DEFAULT_CAP;
    b->data = malloc(sizeof(entry) * b->cap);
    assert(b->data);
    return;
}

void bucket_free(bucket* b) {
    b->len = 0;
    b->cap = 0;
    free(b->data);
    return;
}

void bucket_insert(bucket* b, entry e, size_t at) {
    assert(at >= 0 && at <= b->len);
    if (b->len == b->cap) {
        b->cap = b->cap * 3 / 2;
        b->data = realloc(b->data, sizeof(entry) * b->cap);
        assert(b->data);
    }
    for (size_t next = b->len; next > at; next--) {
        memcpy(&b->data[next], &b->data[next - 1], sizeof(entry));
    }
    b->data[at] = e;
    b->len++;
    return;
}

#define HASHMAP_DEFAULT_LEN 16

void hashmap_new(hashmap* map, hashmap_cmp cmp, hashmap_key_len key_len, hash_func hash) {
    map->len = HASHMAP_DEFAULT_LEN;
    map->count = 0;
    map->hash = hash;
    map->cmp = cmp;
    map->key_len = key_len;
    map->data = calloc(map->len, sizeof(bucket));
    assert(map->data);
    return;
}

void hashmap_free(hashmap* map) {
    for (size_t i = 0; i < map->len; i++) {
        bucket_free(&map->data[i]);
    }
    map->len = 0;
    map->count = 0;
    free(map->data);
    return;
}

uint64_t djb2(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + p[i];
    }
    return hash;
}

size_t hashmap_hash(const hashmap* map, const void* key) {
    switch (map->hash) {
        case HASH_FUNC_IDENTITY:
            return (size_t)key;
        case HASH_FUNC_DJB2:
            return djb2(key, map->key_len(key));
    }
    assert(0);
}

entry hashmap_bucket_add(bucket* buckets, size_t len, entry e, hashmap_cmp cmp, uint64_t hash) {
    bucket* b = &buckets[hash % len];
    if (b->data == NULL) {
        bucket_new(b);
    }
    size_t i;
    for (i = 0; i < b->len; i++) {
        int res = cmp(e.key, b->data[i].key);
        if (res == 0) {
            entry prev = b->data[i];
            b->data[i] = e;
            return prev;
        }
        if (res <= 0) {
            break;
        }
    }
    bucket_insert(b, e, i);
    entry empty = {.key = NULL, .value = NULL};
    return empty;
}

entry hashmap_insert(hashmap* map, void* key, void* value) {
    // twice as many elements as buckets => resize
    if (map->count >= 2 * map->len) {
        size_t next_len = map->len * 3 / 2;
        bucket* next_buckets = calloc(next_len, sizeof(bucket));
        for (size_t i = 0; i < map->len; i++) {
            bucket* current_bucket = map->data + i;
            for (size_t j = 0; j < current_bucket->len; j++) {
                entry e = current_bucket->data[j];
                size_t hash = hashmap_hash(map, e.key);
                hashmap_bucket_add(next_buckets, next_len, e, map->cmp, hash);
            }
            bucket_free(current_bucket);
        }
        free(map->data);
        map->data = next_buckets;
        map->len = next_len;
    }

    uint64_t hash = hashmap_hash(map, key);
    entry e = {.key = key, .value = value};
    entry prev = hashmap_bucket_add(map->data, map->len, e, map->cmp, hash);
    if (!prev.key) {
        map->count++;
    }
    return prev;
}

// returns 1 if found
// return 0 if not found
int hashmap_get(const hashmap* map, const void* key, const void** out) {
    uint64_t hash = hashmap_hash(map, key);
    bucket* b = &map->data[hash % map->len];
    if (b->data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < b->len; i++) {
        const void* current_key = b->data[i].key;
        if (map->cmp(key, current_key) == 0) {
            *out = b->data[i].value;
            return 1;
        }
    }
    return 0;
}

void hashmap_iter(const hashmap* map, void* userdata, void (*f)(entry*, void*)) {
    for (size_t i = 0; i < map->len; i++) {
        bucket* b = &map->data[i];
        for (size_t j = 0; j < b->len; j++) {
            f(&b->data[j], userdata);
        }
    }
}

void** hashmap_keys(const hashmap* map) {
    void** out = malloc(map->count * sizeof(void*));
    assert(out);
    size_t count = 0;
    for (size_t i = 0; i < map->len; i++) {
        bucket* b = &map->data[i];
        for (size_t j = 0; j < b->len; j++) {
            out[count] = b->data[j].key;
            count++;
        }
    }
    return out;
}

int hashmap_strcmp(const void* a, const void* b) {
    return strcmp(a, b);
}

size_t hashmap_strlen(const void* a) {
    return strlen(a);
}
