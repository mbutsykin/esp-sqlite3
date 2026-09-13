# SQLite, the official amalgamation (public domain), as an ESP-IDF component
# with a VFS of its own (vfs.c) rather than the community esp32-idf-sqlite3
# wrapper: that one compiles with SQLITE_NO_SYNC, has an xAccess that reports
# a failed stat as "file missing" (silent data loss), and has no maintainer
# for IDF 5. This VFS is SQLite's demo VFS on POSIX calls, with fsync left in.
#
# Trimmed for one task, one connection, small tables: no threads, no WAL
# (LittleFS has no shared memory to map), no mmap, temp tables in RAM, a
# 4 KiB page to match a LittleFS block. Memory allocation is the app's to
# route (sqlite3_config with SQLITE_CONFIG_MALLOC before sqlite3_initialize).
#
# The amalgamation is 9.5 MB of generated source that sqlite.org publishes
# only as a zip, so it is fetched at configure time into .amalgamation/ and
# checked against the SHA3-256 on https://sqlite.org/download.html, rather
# than committed. Bump the three version lines together (and include/sqlite3.h).
set(SQLITE_ZIP "https://sqlite.org/2026/sqlite-amalgamation-3530400.zip")
set(SQLITE_SHA3 "628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e")
set(SQLITE_SRC "${CMAKE_CURRENT_LIST_DIR}/.amalgamation/sqlite-amalgamation-3530400")

if(NOT EXISTS "${SQLITE_SRC}/sqlite3.c")
    message(STATUS "sqlite3: fetching ${SQLITE_ZIP}")
    set(zip "${CMAKE_CURRENT_LIST_DIR}/.amalgamation/sqlite.zip")
    file(DOWNLOAD "${SQLITE_ZIP}" "${zip}" EXPECTED_HASH SHA3_256=${SQLITE_SHA3} STATUS st)
    list(GET st 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "sqlite3: download failed: ${st}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${CMAKE_CURRENT_LIST_DIR}/.amalgamation")
    file(REMOVE "${zip}")
endif()

idf_component_register(
    SRCS "${SQLITE_SRC}/sqlite3.c" "vfs.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NDEBUG=1
    SQLITE_OS_OTHER=1
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_WAL=1
    SQLITE_MAX_MMAP_SIZE=0
    SQLITE_TEMP_STORE=3
    SQLITE_DEFAULT_PAGE_SIZE=4096
    SQLITE_DEFAULT_CACHE_SIZE=-64
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_LIKE_DOESNT_MATCH_BLOBS=1
    SQLITE_MAX_EXPR_DEPTH=0
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_SHARED_CACHE=1
    SQLITE_OMIT_PROGRESS_CALLBACK=1
    SQLITE_OMIT_DECLTYPE=1
    SQLITE_OMIT_AUTOINIT=1
    SQLITE_SMALL_STACK=1
    SQLITE_USE_ALLOCA=1)

# The amalgamation is not ours to fix warnings in; and its asserts have to go
# (assert_off.h) — see the note there.
set_source_files_properties("${SQLITE_SRC}/sqlite3.c" PROPERTIES
    COMPILE_OPTIONS "-w;-include;${CMAKE_CURRENT_LIST_DIR}/assert_off.h")
