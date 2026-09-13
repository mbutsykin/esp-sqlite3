/*
** The one VFS SQLite has on this device: POSIX calls through ESP-IDF's VFS
** layer, which is how LittleFS (and FAT) are reached. Adapted from SQLite's
** own test_demovfs.c (public domain). Differences from the demo:
**
**   - xTruncate is real: LittleFS supports ftruncate, and a journal that is
**     truncated rather than deleted is what PRAGMA journal_mode=TRUNCATE wants.
**   - xAccess distinguishes "missing" from "stat failed"; the demo (and the
**     esp32-idf-sqlite3 fork) treat any failure as missing, which lets SQLite
**     create a fresh empty database over a real one it could not stat.
**   - No directory sync: LittleFS has no directory handles to fsync, and its
**     metadata commits are atomic anyway.
**   - Time from the system clock, randomness from the hardware RNG.
**
** One task, one connection: locks are no-ops (SQLITE_THREADSAFE=0 above it).
** The journal write-buffer from the demo is kept — SQLite writes a rollback
** journal in small pieces, and LittleFS pays a full program cycle per write.
*/
#include "sqlite3.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "esp_random.h"
#include "esp_timer.h"

#define BUFFERSZ 8192
#define MAXPATHNAME 128

typedef struct EspFile EspFile;
struct EspFile {
  sqlite3_file base; /* must be first */
  int fd;
  char *aBuffer; /* journal write-buffer, or NULL for the database itself */
  int nBuffer;
  sqlite3_int64 iBufferOfst;
};

static int directWrite(EspFile *p, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
  if (lseek(p->fd, (off_t)iOfst, SEEK_SET) != (off_t)iOfst) return SQLITE_IOERR_WRITE;
  if (write(p->fd, zBuf, (size_t)iAmt) != (ssize_t)iAmt) return SQLITE_IOERR_WRITE;
  return SQLITE_OK;
}

static int flushBuffer(EspFile *p) {
  int rc = SQLITE_OK;
  if (p->nBuffer) {
    rc = directWrite(p, p->aBuffer, p->nBuffer, p->iBufferOfst);
    p->nBuffer = 0;
  }
  return rc;
}

static int espClose(sqlite3_file *pFile) {
  EspFile *p = (EspFile *)pFile;
  int rc = flushBuffer(p);
  sqlite3_free(p->aBuffer);
  close(p->fd);
  return rc;
}

static int espRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
  EspFile *p = (EspFile *)pFile;
  int rc = flushBuffer(p);
  if (rc != SQLITE_OK) return rc;
  if (lseek(p->fd, (off_t)iOfst, SEEK_SET) != (off_t)iOfst) return SQLITE_IOERR_READ;
  ssize_t nRead = read(p->fd, zBuf, (size_t)iAmt);
  if (nRead == iAmt) return SQLITE_OK;
  if (nRead >= 0) {
    memset(&((char *)zBuf)[nRead], 0, (size_t)(iAmt - nRead));
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_IOERR_READ;
}

static int espWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
  EspFile *p = (EspFile *)pFile;
  if (!p->aBuffer) return directWrite(p, zBuf, iAmt, iOfst);

  const char *z = (const char *)zBuf;
  int n = iAmt;
  sqlite3_int64 i = iOfst;
  while (n > 0) {
    if (p->nBuffer == BUFFERSZ || p->iBufferOfst + p->nBuffer != i) {
      int rc = flushBuffer(p);
      if (rc != SQLITE_OK) return rc;
    }
    p->iBufferOfst = i - p->nBuffer;
    int nCopy = BUFFERSZ - p->nBuffer;
    if (nCopy > n) nCopy = n;
    memcpy(&p->aBuffer[p->nBuffer], z, (size_t)nCopy);
    p->nBuffer += nCopy;
    n -= nCopy;
    i += nCopy;
    z += nCopy;
  }
  return SQLITE_OK;
}

static int espTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
  EspFile *p = (EspFile *)pFile;
  int rc = flushBuffer(p);
  if (rc != SQLITE_OK) return rc;
  return ftruncate(p->fd, (off_t)size) == 0 ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int espSync(sqlite3_file *pFile, int flags) {
  (void)flags;
  EspFile *p = (EspFile *)pFile;
  int rc = flushBuffer(p);
  if (rc != SQLITE_OK) return rc;
  return fsync(p->fd) == 0 ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

static int espFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
  EspFile *p = (EspFile *)pFile;
  int rc = flushBuffer(p);
  if (rc != SQLITE_OK) return rc;
  struct stat st;
  if (fstat(p->fd, &st) != 0) return SQLITE_IOERR_FSTAT;
  *pSize = st.st_size;
  return SQLITE_OK;
}

static int espLock(sqlite3_file *pFile, int eLock) {
  (void)pFile;
  (void)eLock;
  return SQLITE_OK;
}
static int espUnlock(sqlite3_file *pFile, int eLock) {
  (void)pFile;
  (void)eLock;
  return SQLITE_OK;
}
static int espCheckReservedLock(sqlite3_file *pFile, int *pResOut) {
  (void)pFile;
  *pResOut = 0;
  return SQLITE_OK;
}
static int espFileControl(sqlite3_file *pFile, int op, void *pArg) {
  (void)pFile;
  (void)op;
  (void)pArg;
  return SQLITE_NOTFOUND;
}
static int espSectorSize(sqlite3_file *pFile) {
  (void)pFile;
  return 4096;
}
static int espDeviceCharacteristics(sqlite3_file *pFile) {
  (void)pFile;
  /* LittleFS commits a file atomically per write and never leaves a torn
  ** page behind, and a truncated journal is enough to say "rolled back". */
  return SQLITE_IOCAP_SAFE_APPEND | SQLITE_IOCAP_SEQUENTIAL;
}

static int espOpen(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile, int flags,
                   int *pOutFlags) {
  static const sqlite3_io_methods io = {
      1,        espClose,  espRead,   espWrite,         espTruncate, espSync,
      espFileSize, espLock, espUnlock, espCheckReservedLock, espFileControl,
      espSectorSize, espDeviceCharacteristics};
  (void)pVfs;
  EspFile *p = (EspFile *)pFile;
  if (zName == 0) return SQLITE_IOERR; /* SQLITE_TEMP_STORE=3: never asked */

  char *aBuf = 0;
  if (flags & SQLITE_OPEN_MAIN_JOURNAL) {
    aBuf = (char *)sqlite3_malloc(BUFFERSZ);
    if (!aBuf) return SQLITE_NOMEM;
  }

  int oflags = 0;
  if (flags & SQLITE_OPEN_EXCLUSIVE) oflags |= O_EXCL;
  if (flags & SQLITE_OPEN_CREATE) oflags |= O_CREAT;
  if (flags & SQLITE_OPEN_READONLY) oflags |= O_RDONLY;
  if (flags & SQLITE_OPEN_READWRITE) oflags |= O_RDWR;

  memset(p, 0, sizeof(EspFile));
  p->fd = open(zName, oflags, 0600);
  if (p->fd < 0) {
    sqlite3_free(aBuf);
    return SQLITE_CANTOPEN;
  }
  p->aBuffer = aBuf;
  if (pOutFlags) *pOutFlags = flags;
  p->base.pMethods = &io;
  return SQLITE_OK;
}

static int espDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
  (void)pVfs;
  (void)dirSync;
  if (unlink(zPath) != 0 && errno != ENOENT) return SQLITE_IOERR_DELETE;
  return SQLITE_OK;
}

static int espAccess(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut) {
  (void)pVfs;
  (void)flags; /* everything we can see we can read and write */
  struct stat st;
  if (stat(zPath, &st) == 0) {
    *pResOut = 1;
    return SQLITE_OK;
  }
  if (errno == ENOENT) {
    *pResOut = 0;
    return SQLITE_OK;
  }
  return SQLITE_IOERR_ACCESS;
}

static int espFullPathname(sqlite3_vfs *pVfs, const char *zPath, int nPathOut, char *zPathOut) {
  (void)pVfs;
  /* Every path the app opens is already absolute under a mount point. */
  sqlite3_snprintf(nPathOut, zPathOut, "%s", zPath);
  zPathOut[nPathOut - 1] = '\0';
  return SQLITE_OK;
}

static int espRandomness(sqlite3_vfs *pVfs, int nByte, char *zByte) {
  (void)pVfs;
  esp_fill_random(zByte, (size_t)nByte);
  return nByte;
}

static int espSleep(sqlite3_vfs *pVfs, int nMicro) {
  (void)pVfs;
  usleep((useconds_t)nMicro);
  return nMicro;
}

static int espCurrentTime(sqlite3_vfs *pVfs, double *pTime) {
  (void)pVfs;
  time_t t = time(0);
  *pTime = t / 86400.0 + 2440587.5;
  return SQLITE_OK;
}

static sqlite3_vfs espVfs = {
    1,                /* iVersion */
    sizeof(EspFile),  /* szOsFile */
    MAXPATHNAME,      /* mxPathname */
    0,                /* pNext */
    "esp",            /* zName */
    0,                /* pAppData */
    espOpen,          espDelete, espAccess, espFullPathname,
    0, 0, 0, 0,       /* xDlOpen, xDlError, xDlSym, xDlClose */
    espRandomness,    espSleep, espCurrentTime,
    0,                /* xGetLastError */
};

/* SQLITE_OS_OTHER: the core calls these instead of bringing its own OS layer. */
int sqlite3_os_init(void) { return sqlite3_vfs_register(&espVfs, 1); }
int sqlite3_os_end(void) { return SQLITE_OK; }
