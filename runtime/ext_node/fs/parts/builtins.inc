/* -- shared object builders (defined in fs_ops.c) ------------------------- */

xt_value xt_node_stat_result(const xt_fs_stat_t *info);
xt_value xt_node_dirent_result(const char *dir, const char *name);

/* -- synchronous fs builtins (one translation unit per group) ------------- */

xt_value xt_node_read_text_file(int32_t argc, xt_value *argv);
xt_value xt_node_write_file(int32_t argc, xt_value *argv);
xt_value xt_node_append_file(int32_t argc, xt_value *argv);
xt_value xt_node_exists(int32_t argc, xt_value *argv);
xt_value xt_node_mkdir(int32_t argc, xt_value *argv);
xt_value xt_node_read_dir(int32_t argc, xt_value *argv);
xt_value xt_node_rm(int32_t argc, xt_value *argv);
xt_value xt_node_unlink(int32_t argc, xt_value *argv);
xt_value xt_node_rmdir(int32_t argc, xt_value *argv);
xt_value xt_node_rename(int32_t argc, xt_value *argv);
xt_value xt_node_copy_file(int32_t argc, xt_value *argv);
xt_value xt_node_realpath(int32_t argc, xt_value *argv);
xt_value xt_node_stat(int32_t argc, xt_value *argv);
xt_value xt_node_lstat(int32_t argc, xt_value *argv);
xt_value xt_node_cp(int32_t argc, xt_value *argv);

/* fd_ops.c */
xt_value xt_node_open(int32_t argc, xt_value *argv);
xt_value xt_node_close(int32_t argc, xt_value *argv);
xt_value xt_node_read(int32_t argc, xt_value *argv);
xt_value xt_node_write(int32_t argc, xt_value *argv);
xt_value xt_node_readv(int32_t argc, xt_value *argv);
xt_value xt_node_writev(int32_t argc, xt_value *argv);
xt_value xt_node_fstat(int32_t argc, xt_value *argv);
xt_value xt_node_fsync(int32_t argc, xt_value *argv);
xt_value xt_node_fdatasync(int32_t argc, xt_value *argv);
xt_value xt_node_ftruncate(int32_t argc, xt_value *argv);
xt_value xt_node_fchmod(int32_t argc, xt_value *argv);
xt_value xt_node_fchown(int32_t argc, xt_value *argv);
xt_value xt_node_futimes(int32_t argc, xt_value *argv);

/* meta_ops.c */
xt_value xt_node_access(int32_t argc, xt_value *argv);
xt_value xt_node_chmod(int32_t argc, xt_value *argv);
xt_value xt_node_chown(int32_t argc, xt_value *argv);
xt_value xt_node_lchmod(int32_t argc, xt_value *argv);
xt_value xt_node_lchown(int32_t argc, xt_value *argv);
xt_value xt_node_truncate(int32_t argc, xt_value *argv);
xt_value xt_node_utimes(int32_t argc, xt_value *argv);
xt_value xt_node_lutimes(int32_t argc, xt_value *argv);
xt_value xt_node_statfs(int32_t argc, xt_value *argv);
xt_value xt_node_mkdtemp(int32_t argc, xt_value *argv);

/* link_ops.c */
xt_value xt_node_link(int32_t argc, xt_value *argv);
xt_value xt_node_symlink(int32_t argc, xt_value *argv);
xt_value xt_node_readlink(int32_t argc, xt_value *argv);

/* dir.c */
xt_value xt_node_opendir(int32_t argc, xt_value *argv);

/* glob.c */
xt_value xt_node_glob(int32_t argc, xt_value *argv);

/* watch.c */
xt_value xt_node_watch(int32_t argc, xt_value *argv);
xt_value xt_node_watch_file(int32_t argc, xt_value *argv);
xt_value xt_node_unwatch_file(int32_t argc, xt_value *argv);

/* constants.c */
xt_value xt_fs_constants(void);
xt_value xt_fs_promises(void);
