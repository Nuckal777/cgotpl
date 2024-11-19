#include "map.h"

#include <stdio.h>
#include <string.h>

#include "test.h"

int map_strcmp(const void* a, const void* b) {
    return strcmp(a, b);
}

size_t map_strlen(const void* a) {
    return strlen(a);
}

nutest_result map_add_one(void) {
    hashmap map;
    hashmap_new(&map, map_strcmp, map_strlen, HASH_FUNC_DJB2);
    entry prev = hashmap_insert(&map, "abc", "xyz");
    NUTEST_ASSERT(prev.key == NULL);
    NUTEST_ASSERT(map.count == 1);
    const char* result = NULL;
    int success = hashmap_get(&map, "abc", (const void**)&result);
    NUTEST_ASSERT(success);
    NUTEST_ASSERT(result);
    NUTEST_ASSERT(strcmp(result, "xyz") == 0);
    hashmap_free(&map);
    return NUTEST_PASS;
}

nutest_result map_add_prev(void) {
    hashmap map;
    hashmap_new(&map, map_strcmp, map_strlen, HASH_FUNC_DJB2);
    entry prev = hashmap_insert(&map, "def", "456");
    NUTEST_ASSERT(prev.key == NULL);
    NUTEST_ASSERT(map.count == 1);
    prev = hashmap_insert(&map, "def", "ghi");
    NUTEST_ASSERT(prev.key != NULL);
    NUTEST_ASSERT(prev.value != NULL);
    NUTEST_ASSERT(strcmp((const char*) prev.key, "def") == 0);
    NUTEST_ASSERT(strcmp((const char*) prev.value, "456") == 0);
    NUTEST_ASSERT(map.count == 1);
    const char* result = NULL;
    int success = hashmap_get(&map, "def", (const void**)&result);
    NUTEST_ASSERT(success);
    NUTEST_ASSERT(result);
    NUTEST_ASSERT(strcmp(result, "ghi") == 0);
    hashmap_free(&map);
    return NUTEST_PASS;
}

nutest_result map_miss(void) {
    hashmap map;
    hashmap_new(&map, map_strcmp, map_strlen, HASH_FUNC_DJB2);
    entry prev = hashmap_insert(&map, "apple", "banana");
    NUTEST_ASSERT(prev.key == NULL);
    NUTEST_ASSERT(map.count == 1);
    const char* result = NULL;
    int success = hashmap_get(&map, "pear", (const void**)&result);
    NUTEST_ASSERT(!success);
    NUTEST_ASSERT(result == NULL);
    hashmap_free(&map);
    return NUTEST_PASS;
}

nutest_result map_add_few(void) {
    hashmap map;
    hashmap_new(&map, map_strcmp, map_strlen, HASH_FUNC_DJB2);
    char* keys[6] = {"a", "c", "e", "g", "i", "k"};
    char* vals[6] = {"b", "d", "f", "h", "j", "l"};
    for (size_t i = 0; i < 6; i++) {
        entry prev = hashmap_insert(&map, keys[i], vals[i]);
        NUTEST_ASSERT(prev.key == NULL);
    }
    for (size_t i = 0; i < 6; i++) {
        const char* result = NULL;
        int success = hashmap_get(&map, keys[i], (const void**)&result);
        NUTEST_ASSERT(success);
        NUTEST_ASSERT(strcmp(result, vals[i]) == 0);
    }
    NUTEST_ASSERT(map.count == 6);
    hashmap_free(&map);
    return NUTEST_PASS;
}

int map_sizetcmp(const void* a, const void* b) {
    size_t x = (size_t)a;
    size_t y = (size_t)b;
    if (x < y) {
        return -1;
    }
    if (x == y) {
        return 0;
    }
    return 1;
}

size_t map_sizetlen(const void* a) {
    return sizeof(size_t);
}

nutest_result map_add_many(void) {
    hashmap map;
    hashmap_new(&map, map_sizetcmp, map_sizetlen, HASH_FUNC_IDENTITY);
    size_t count = 1000;
    for (size_t i = 1; i < count + 1; i++) {
        entry prev = hashmap_insert(&map, (void*)(2 * i), (void*)(2 * i + 1));
        NUTEST_ASSERT(prev.key == NULL);
    }
    for (size_t i = 1; i < count + 1; i++) {
        size_t result = -1;
        int success = hashmap_get(&map, (void*)(2 * i), (const void**)&result);
        NUTEST_ASSERT(success);
        NUTEST_ASSERT(result == 2 * i + 1);
    }
    NUTEST_ASSERT(map.count == count);
    hashmap_free(&map);
    return NUTEST_PASS;
}

void map_sum(entry* e, void* userdata) {
    size_t* sum = (size_t*)userdata;
    *sum += (size_t)e->key;
    *sum += (size_t)e->value;
}

nutest_result map_iter(void) {
    hashmap map;
    hashmap_new(&map, map_sizetcmp, map_sizetlen, HASH_FUNC_IDENTITY);
    entry prev = hashmap_insert(&map, (void*)1, (void*)2);
    NUTEST_ASSERT(prev.key == NULL);
    prev = hashmap_insert(&map, (void*)3, (void*)4);
    NUTEST_ASSERT(prev.key == NULL);
    size_t sum = 0;
    hashmap_iter(&map, &sum, map_sum);
    NUTEST_ASSERT(sum == 10);
    hashmap_free(&map);
    return NUTEST_PASS;
}

nutest_result map_keys(void) {
    hashmap map;
    hashmap_new(&map, map_sizetcmp, map_sizetlen, HASH_FUNC_IDENTITY);
    entry prev = hashmap_insert(&map, (void*)58, (void*)97);
    NUTEST_ASSERT(prev.key == NULL);
    void** keys = hashmap_keys(&map);
    size_t key = (size_t)keys[0];
    NUTEST_ASSERT(key == 58);
    free(keys);
    hashmap_free(&map);
    return NUTEST_PASS;
}

int main() {
    nutest_register(map_add_one);
    nutest_register(map_add_prev);
    nutest_register(map_miss);
    nutest_register(map_add_few);
    nutest_register(map_add_many);
    nutest_register(map_iter);
    nutest_register(map_keys);
    return nutest_run();
}
