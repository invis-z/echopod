#pragma once

#include <stdint.h>

#define RIPPL_BORROWED __attribute__((__may_alias__))

typedef uint32_t RIPPL_BORROWED BorrowedWord;
