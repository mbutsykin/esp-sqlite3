// A stable path for the fetched header: build systems that collect include
// directories before the configure step runs (PlatformIO does) need this
// file to exist in the repo. Bump with the version in CMakeLists.txt.
#pragma once
#include "../.amalgamation/sqlite-amalgamation-3530400/sqlite3.h"
