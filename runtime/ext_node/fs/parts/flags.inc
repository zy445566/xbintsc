/* -- open-flag parsing ---------------------------------------------------- */

#if defined(_WIN32)
#define XT_FS_BINARY_FLAG _O_BINARY
#define XT_FS_RDONLY _O_RDONLY
#define XT_FS_WRONLY _O_WRONLY
#define XT_FS_RDWR _O_RDWR
#define XT_FS_CREAT _O_CREAT
#define XT_FS_TRUNC _O_TRUNC
#define XT_FS_APPEND _O_APPEND
#define XT_FS_EXCL _O_EXCL
#else
#define XT_FS_BINARY_FLAG 0
#define XT_FS_RDONLY O_RDONLY
#define XT_FS_WRONLY O_WRONLY
#define XT_FS_RDWR O_RDWR
#define XT_FS_CREAT O_CREAT
#define XT_FS_TRUNC O_TRUNC
#define XT_FS_APPEND O_APPEND
#define XT_FS_EXCL O_EXCL
#endif

/* Translate a Node flag string (`"r"`, `"w+"`, `"a"`, ...) or number. */
static inline int xt_fs_parse_flags(xt_value value) {
  if (XT_IS_STRING(value)) {
    const char *flag = xt_string_data(value);
    if (!flag) return XT_FS_RDONLY | XT_FS_BINARY_FLAG;
    int plus = strchr(flag, '+') != NULL;
    int excl = strchr(flag, 'x') != NULL;
    int base;
    if (flag[0] == 'a') base = XT_FS_APPEND | XT_FS_CREAT;
    else if (flag[0] == 'w') base = XT_FS_CREAT | XT_FS_TRUNC;
    else base = 0; /* 'r' and the numeric path below */
    base |= (plus || flag[0] == 'w' || flag[0] == 'a') ? XT_FS_RDWR : XT_FS_RDONLY;
    if (excl) base |= XT_FS_EXCL;
    return base | XT_FS_BINARY_FLAG;
  }
  return (int)xt_to_number(value) | XT_FS_BINARY_FLAG;
}
