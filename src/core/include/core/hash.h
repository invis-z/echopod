#pragma once

#include <stddef.h>
#include <stdint.h>

#define HASH_START UINT32_C(2166136261)
#define HASH_PRIME UINT32_C(16777619)

#define HASH64_START UINT64_C(14695981039346656037)
#define HASH64_PRIME UINT64_C(1099511628211)

uint32_t hash_update(uint32_t hash, const void *bytes, size_t length);
uint64_t hash64_update(uint64_t hash, const void *bytes, size_t length);
