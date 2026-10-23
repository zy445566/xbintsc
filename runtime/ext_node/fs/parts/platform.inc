/* -- platform shims ------------------------------------------------------- */

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Stat results are filled from Win32 directly. The CRT's `_stat` converts the
 * FILETIME through the *local* time zone before producing `time_t`, so a time
 * near the Unix epoch (e.g. `utimesSync(path, 1000, 2000)`) maps to 1969 on a
 * machine west of UTC and the CRT reports an unrepresentable `st_mtime` (-1).
 * Reading the Win32 FILETIME and converting it to Unix seconds in UTC like
 * libuv/Node keeps the values exact and timezone independent. */
typedef struct {
  long long st_size;
  unsigned int st_mode;
  unsigned int st_uid;
  unsigned int st_gid;
  unsigned int st_dev;
  unsigned long long st_ino;
  unsigned int st_nlink;
  unsigned int st_rdev;
  long long st_mtime; /* seconds since the Unix epoch */
  long long st_atime;
  long long st_ctime;
} xt_fs_stat_t;

int xt_fs_win_stat(const char *path, xt_fs_stat_t *out);
int xt_fs_win_lstat(const char *path, xt_fs_stat_t *out);
int xt_fs_win_fstat(int fd, xt_fs_stat_t *out);
#define xt_fs_stat_fn xt_fs_win_stat
#define xt_fs_lstat_fn xt_fs_win_lstat
#define xt_fs_fstat_fn xt_fs_win_fstat
#define xt_fs_access _access
#define xt_fs_mkdir_mode(path, mode) _mkdir(path)
#define xt_fs_mkdir_one(path) _mkdir(path)
#define xt_fs_rmdir_one(path) _rmdir(path)
#define XT_FS_OPEN _open
#define XT_FS_CLOSE _close
#define XT_FS_READ _read
#define XT_FS_WRITE _write

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) 0
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(m) 0
#endif
#ifndef S_ISBLK
#define S_ISBLK(m) 0
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & _S_IFMT) == _S_IFCHR)
#endif
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct stat xt_fs_stat_t;
#define xt_fs_stat_fn stat
#define xt_fs_lstat_fn lstat
#define xt_fs_access access
#define xt_fs_mkdir_mode(path, mode) mkdir((path), (mode))
#define xt_fs_mkdir_one(path) mkdir(path, 0777)
#define xt_fs_rmdir_one(path) rmdir(path)
#define XT_FS_OPEN open
#define XT_FS_CLOSE close
#define XT_FS_READ read
#define XT_FS_WRITE write
#endif

/* -- recursive directory creation ----------------------------------------- */
/* `mkdir -p` splits a path at its separators and creates every prefix. A
 * Windows volume prefix must be skipped: `_mkdir("C:")` is not "the C: root" but
 * a drive-relative path that resolves against that drive's current directory,
 * so it fails with EACCES. Creating the prefixes of `C:\...` (e.g. anything
 * under %TEMP%) used to abort on that first component while paths on the
 * process's current drive appeared to work. */

static inline int xt_fs_is_sep(char c) {
#if defined(_WIN32)
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

/* Byte index of the first directory component that may be created: 0 for a
 * relative or POSIX path, past `C:` / `\\server\share` on Windows. */
static inline size_t xt_fs_path_root(const char *path) {
#if defined(_WIN32)
  if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/')) {
    size_t i = 2;
    for (int separators = 0; separators < 2 && path[i]; ) {
      if (xt_fs_is_sep(path[i])) separators++;
      i++;
    }
    return i;
  }
  if (path[0] != '\0' && path[1] == ':') {
    size_t i = 2;
    while (xt_fs_is_sep(path[i])) i++;
    return i;
  }
#endif
  return 0;
}

/* Create `path` and every missing ancestor, tolerating existing directories.
 * Returns 0 or -1 with `errno` set by the failed `mkdir`. */
static inline int xt_fs_mkdir_parents(const char *path, int mode) {
  size_t length = strlen(path);
  char *copy = (char *)malloc(length + 1);
  if (!copy) return -1;
  memcpy(copy, path, length + 1);
  while (length > 1 && xt_fs_is_sep(copy[length - 1])) copy[--length] = '\0';

  int status = 0;
  size_t start = xt_fs_path_root(copy);
  if (start < length) {
    /* A driver prefix is already consumed; otherwise index 0 is a separator
     * for absolute paths and the first byte for relative ones. */
    for (size_t i = start > 0 ? start : 1; i <= length; i++) {
      if (!xt_fs_is_sep(copy[i]) && copy[i] != '\0') continue;
      char saved = copy[i];
      copy[i] = '\0';
      if (xt_fs_mkdir_mode(copy, mode) != 0 && errno != EEXIST) {
        status = -1;
        break;
      }
      copy[i] = saved;
    }
  }
  free(copy);
  return status;
}
