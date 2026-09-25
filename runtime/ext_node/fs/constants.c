/*
 * `fs.constants` for xbintsc.
 *
 * The numeric values mirror the host platform's `<fcntl.h>` / `<sys/stat.h>`
 * (which is what Node reports on that platform). Constants a platform does not
 * define are omitted rather than faked.
 */

#include "../node_common.h"
#include "fs_common.h"

#include <sys/stat.h>

#if defined(_WIN32)
#include <fcntl.h>
#endif

#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif

#define XT_FS_CONST(name, value) xt_object_set(object, xt_string_from_cstr(name), xt_number((double)(value)))

xt_value xt_fs_constants(void) {
  xt_value object = xt_object_new();

  XT_FS_CONST("F_OK", F_OK);
  XT_FS_CONST("R_OK", R_OK);
  XT_FS_CONST("W_OK", W_OK);
  XT_FS_CONST("X_OK", X_OK);

  XT_FS_CONST("COPYFILE_EXCL", 1);
  XT_FS_CONST("COPYFILE_FICLONE", 2);
  XT_FS_CONST("COPYFILE_FICLONE_FORCE", 4);

#ifdef O_RDONLY
  XT_FS_CONST("O_RDONLY", O_RDONLY);
#endif
#ifdef O_WRONLY
  XT_FS_CONST("O_WRONLY", O_WRONLY);
#endif
#ifdef O_RDWR
  XT_FS_CONST("O_RDWR", O_RDWR);
#endif
#ifdef O_CREAT
  XT_FS_CONST("O_CREAT", O_CREAT);
#elif defined(_O_CREAT)
  XT_FS_CONST("O_CREAT", _O_CREAT);
#endif
#ifdef O_EXCL
  XT_FS_CONST("O_EXCL", O_EXCL);
#elif defined(_O_EXCL)
  XT_FS_CONST("O_EXCL", _O_EXCL);
#endif
#ifdef O_TRUNC
  XT_FS_CONST("O_TRUNC", O_TRUNC);
#elif defined(_O_TRUNC)
  XT_FS_CONST("O_TRUNC", _O_TRUNC);
#endif
#ifdef O_APPEND
  XT_FS_CONST("O_APPEND", O_APPEND);
#elif defined(_O_APPEND)
  XT_FS_CONST("O_APPEND", _O_APPEND);
#endif
#ifdef O_NOCTTY
  XT_FS_CONST("O_NOCTTY", O_NOCTTY);
#endif
#ifdef O_DIRECTORY
  XT_FS_CONST("O_DIRECTORY", O_DIRECTORY);
#endif
#ifdef O_NOFOLLOW
  XT_FS_CONST("O_NOFOLLOW", O_NOFOLLOW);
#endif
#ifdef O_SYNC
  XT_FS_CONST("O_SYNC", O_SYNC);
#endif
#ifdef O_DSYNC
  XT_FS_CONST("O_DSYNC", O_DSYNC);
#endif
#ifdef O_NONBLOCK
  XT_FS_CONST("O_NONBLOCK", O_NONBLOCK);
#endif
#ifdef O_SYMLINK
  XT_FS_CONST("O_SYMLINK", O_SYMLINK);
#endif

#if defined(_WIN32)
  XT_FS_CONST("S_IFMT", _S_IFMT);
  XT_FS_CONST("S_IFREG", _S_IFREG);
  XT_FS_CONST("S_IFDIR", _S_IFDIR);
  XT_FS_CONST("S_IFCHR", _S_IFCHR);
#else
  XT_FS_CONST("S_IFMT", S_IFMT);
  XT_FS_CONST("S_IFREG", S_IFREG);
  XT_FS_CONST("S_IFDIR", S_IFDIR);
  XT_FS_CONST("S_IFCHR", S_IFCHR);
  XT_FS_CONST("S_IFBLK", S_IFBLK);
  XT_FS_CONST("S_IFIFO", S_IFIFO);
  XT_FS_CONST("S_IFLNK", S_IFLNK);
  XT_FS_CONST("S_IFSOCK", S_IFSOCK);
#endif

  return object;
}
