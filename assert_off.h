// Force-included ahead of sqlite3.c (see CMakeLists). ESP-IDF's assert.h
// keeps assert() evaluating its expression under NDEBUG
// (CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE), and SQLite's asserts reference
// debug-only functions that NDEBUG compiles out — so they have to vanish
// entirely here, as SQLite itself intends for a release build.
#pragma once
#include <assert.h>
#undef assert
#define assert(x) ((void)0)
