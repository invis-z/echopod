#pragma once

#include <stdint.h>

#define ECHO_BORROWED __attribute__((__may_alias__))

typedef uint32_t ECHO_BORROWED BorrowedWord;
