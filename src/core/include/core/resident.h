#pragma once

#ifdef RIPPL_RESIDENT_TABLES
#define RIPPL_RESIDENT __attribute__((section(".resident")))
#define RIPPL_CODE2 __attribute__((section(".code2"), noinline))
#else
#define RIPPL_RESIDENT
#define RIPPL_CODE2
#endif
