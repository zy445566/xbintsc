/*
 * Node.js `constants` module for xbintsc (deprecated in Node, kept for older
 * packages such as `graceful-fs`).
 *
 * Each named constant is a value getter; the numeric values mirror the host's
 * `<fcntl.h>` where available and fall back to the POSIX numbering otherwise.
 */

#include "../node_common.h"

#include <sys/stat.h>

#if defined(_WIN32)
#include <fcntl.h>
#endif

#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif
#ifndef O_CREAT
#define O_CREAT 64
#endif
#ifndef O_EXCL
#define O_EXCL 128
#endif
#ifndef O_TRUNC
#define O_TRUNC 512
#endif
#ifndef O_APPEND
#define O_APPEND 1024
#endif
#ifndef O_SYMLINK
#define O_SYMLINK 4096
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

#define XT_CONST_GETTER(name, value)                                           \
  xt_value name(int32_t argc, xt_value *argv) {                                \
    (void)argc;                                                                \
    (void)argv;                                                                \
    return xt_number((double)(value));                                         \
  }

XT_CONST_GETTER(xt_constants_f_ok, F_OK)
XT_CONST_GETTER(xt_constants_r_ok, R_OK)
XT_CONST_GETTER(xt_constants_w_ok, W_OK)
XT_CONST_GETTER(xt_constants_x_ok, X_OK)
XT_CONST_GETTER(xt_constants_o_rdonly, O_RDONLY)
XT_CONST_GETTER(xt_constants_o_wronly, O_WRONLY)
XT_CONST_GETTER(xt_constants_o_rdwr, O_RDWR)
XT_CONST_GETTER(xt_constants_o_creat, O_CREAT)
XT_CONST_GETTER(xt_constants_o_excl, O_EXCL)
XT_CONST_GETTER(xt_constants_o_trunc, O_TRUNC)
XT_CONST_GETTER(xt_constants_o_append, O_APPEND)
XT_CONST_GETTER(xt_constants_o_symlink, O_SYMLINK)
