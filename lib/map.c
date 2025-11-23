#include "map.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HASHMAP_DEFAULT_CAP 24

void hashmap_new(hashmap* map, hashmap_cmp cmp, hashmap_key_len key_len, hash_func hash) {
    map->len = HASHMAP_DEFAULT_CAP;
    map->data = calloc(map->len, sizeof(entry));
    assert(map->data);
    map->count = 0;
    map->cmp = cmp;
    map->hash = hash;
    map->key_len = key_len;
}

void hashmap_free(hashmap* map) {
    free(map->data);
    map->len = 0;
    map->count = 0;
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
    return 0;
}

entry hashmap_insert(hashmap* map, void* key, void* value) {
    if (map->count * 10 / map->len > 7) {
        size_t old_len = map->len;
        entry* old_data = map->data;
        map->len = map->len * 3 / 2;
        map->data = calloc(map->len, sizeof(entry));
        map->count = 0;
        assert(map->data);
        for (entry* entry = old_data; entry < old_data + old_len; entry++) {
            if (entry->exists) {
                hashmap_insert(map, entry->key, entry->value);
            }
        }
        free(old_data);
    }

    uint64_t hash = hashmap_hash(map, key);
    size_t start = hash % map->len;
    for (size_t counter = 0; counter < map->len; counter++) {
        size_t idx = (start + counter) % map->len;
        entry* current = map->data + idx;
        if (!current->exists) {
            current->exists = true;
            current->key = key;
            current->value = value;
            map->count++;
            return (entry){.exists = false};
        }
        if (map->cmp(key, current->key) == 0) {
            entry prev = *current;
            current->value = value;
            current->key = key;
            return prev;
        }
    }
    assert(0);
    return (entry){};
}

int hashmap_get(const hashmap* map, const void* key, const void** out) {
    uint64_t hash = hashmap_hash(map, key);
    size_t start = hash % map->len;
    for (size_t counter = 0; counter < map->len; counter++) {
        size_t idx = (start + counter) % map->len;
        entry* current = map->data + idx;
        if(!current->exists) {
            return 0;
        }
        if (map->cmp(key, current->key) == 0) {
            *out = current->value;
            return 1;
        }
    }
    return 0;
}

void hashmap_iter(const hashmap* map, void* userdata, void (*f)(entry*, void*)) {
    for(entry* current = map->data; current < map->data + map->len; current++) {
        if (current->exists) {
            entry entry = {.key = current->key, .value = current->value};
            f(&entry, userdata);
        }
    }
}

void** hashmap_keys(const hashmap* map) {
    void** out = malloc(map->count * sizeof(void*));
    assert(out);
    size_t count = 0;
    for(entry* current = map->data; current < map->data + map->len; current++) {
        if (current->exists) {
            out[count] = current->key;
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
