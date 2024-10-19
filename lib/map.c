#include "map.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t xxh3(const void* data, size_t len, uint64_t seed);

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
#define HASHMAP_SEED 1337

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

size_t hashmap_hash(hashmap* map, const void* key) {
    switch (map->hash) {
        case HASH_FUNC_IDENTITY:
            return (size_t)key;
        case HASH_FUNC_XXH3:
            return xxh3(key, map->key_len(key), HASHMAP_SEED);
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
int hashmap_get(hashmap* map, const void* key, const void** out) {
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

void hashmap_iter(hashmap* map, void* userdata, void (*f)(entry*, void*)) {
    for (size_t i = 0; i < map->len; i++) {
        bucket* b = &map->data[i];
        for (size_t j = 0; j < b->len; j++) {
            f(&b->data[j], userdata);
        }
    }
}

//-----------------------------------------------------------------------------
// xxHash Library
// Copyright (c) 2012-2024 Yann Collet
// All rights reserved.
//
// BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
//
// xxHash3
//-----------------------------------------------------------------------------
#define XXH_PRIME_1 11400714785074694791ULL
#define XXH_PRIME_2 14029467366897019727ULL
#define XXH_PRIME_3 1609587929392839161ULL
#define XXH_PRIME_4 9650029242287828579ULL
#define XXH_PRIME_5 2870177450012600261ULL

static uint64_t XXH_read64(const void* memptr) {
    uint64_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static uint32_t XXH_read32(const void* memptr) {
    uint32_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static uint64_t XXH_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static uint64_t xxh3(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME_1 + XXH_PRIME_2;
        uint64_t v2 = seed + XXH_PRIME_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME_1;

        do {
            v1 += XXH_read64(p) * XXH_PRIME_2;
            v1 = XXH_rotl64(v1, 31);
            v1 *= XXH_PRIME_1;

            v2 += XXH_read64(p + 8) * XXH_PRIME_2;
            v2 = XXH_rotl64(v2, 31);
            v2 *= XXH_PRIME_1;

            v3 += XXH_read64(p + 16) * XXH_PRIME_2;
            v3 = XXH_rotl64(v3, 31);
            v3 *= XXH_PRIME_1;

            v4 += XXH_read64(p + 24) * XXH_PRIME_2;
            v4 = XXH_rotl64(v4, 31);
            v4 *= XXH_PRIME_1;

            p += 32;
        } while (p <= limit);

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);

        v1 *= XXH_PRIME_2;
        v1 = XXH_rotl64(v1, 31);
        v1 *= XXH_PRIME_1;
        h64 ^= v1;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v2 *= XXH_PRIME_2;
        v2 = XXH_rotl64(v2, 31);
        v2 *= XXH_PRIME_1;
        h64 ^= v2;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v3 *= XXH_PRIME_2;
        v3 = XXH_rotl64(v3, 31);
        v3 *= XXH_PRIME_1;
        h64 ^= v3;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;

        v4 *= XXH_PRIME_2;
        v4 = XXH_rotl64(v4, 31);
        v4 *= XXH_PRIME_1;
        h64 ^= v4;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
    } else {
        h64 = seed + XXH_PRIME_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = XXH_read64(p);
        k1 *= XXH_PRIME_2;
        k1 = XXH_rotl64(k1, 31);
        k1 *= XXH_PRIME_1;
        h64 ^= k1;
        h64 = XXH_rotl64(h64, 27) * XXH_PRIME_1 + XXH_PRIME_4;
        p += 8;
    }

    if (p + 4 <= end) {
        h64 ^= (uint64_t)(XXH_read32(p)) * XXH_PRIME_1;
        h64 = XXH_rotl64(h64, 23) * XXH_PRIME_2 + XXH_PRIME_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p) * XXH_PRIME_5;
        h64 = XXH_rotl64(h64, 11) * XXH_PRIME_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME_3;
    h64 ^= h64 >> 32;

    return h64;
}
