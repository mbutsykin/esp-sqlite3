# esp-sqlite3

The official [SQLite](https://sqlite.org) amalgamation (public domain) as an
ESP-IDF component, with a small VFS on POSIX calls so it runs on any filesystem
ESP-IDF mounts — LittleFS, SPIFFS, FAT. Unlike `esp32-idf-sqlite3`, `fsync` is
real, `xTruncate` works, and `xAccess` tells a missing file from a failed stat.

The amalgamation itself is not in this repo: CMake fetches the pinned zip from
sqlite.org at configure time and verifies it against the published SHA3-256.

Built single-threaded, without WAL or mmap, temp tables in RAM, 4 KiB pages.
Allocation is left to the app: call `sqlite3_config(SQLITE_CONFIG_MALLOC, …)`
before `sqlite3_initialize()` to put the page cache in PSRAM.

```yaml
# idf_component.yml
dependencies:
  sqlite3:
    git: https://github.com/mbutsykin/esp-sqlite3.git
    version: "v3.53.4"
```

```c
sqlite3_initialize();
sqlite3 *db;
sqlite3_open_v2("/littlefs/app.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
```

The version tag tracks the SQLite release inside.
