#pragma once

#ifdef RIPPL_RESIDENT_TABLES
#define RIPPL_RESIDENT __attribute__((section(".resident")))
#else
#define RIPPL_RESIDENT
#endif

#ifdef RIPPL_NP_FACES_SECTIONS
#define RIPPL_NP_FACES_CODE __attribute__((section(".np_faces.text")))
#define RIPPL_NP_FACES_DATA __attribute__((section(".np_faces.rodata")))
#define RIPPL_NP_FACES_ENTRY __attribute__((section(".np_faces.entry"), used))
#else
#define RIPPL_NP_FACES_CODE
#define RIPPL_NP_FACES_DATA
#define RIPPL_NP_FACES_ENTRY
#endif
