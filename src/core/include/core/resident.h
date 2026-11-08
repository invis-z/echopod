#pragma once

#ifdef RIPPL_RESIDENT_TABLES
#define RIPPL_RESIDENT __attribute__((section(".resident")))
#else
#define RIPPL_RESIDENT
#endif
