/*
 * Main-thread argument marshalling for asynchronous `fs/promises`.
 *
 * Each export copies whatever it needs from the JavaScript values (paths,
 * encodings, buffers, options) into a job, then submits it. The blocking half
 * runs on a worker; the promise is settled later on the reactor thread. This is
 * also where the non-I/O argument parsing matches the synchronous `fs` module.
 */

#include "async_job.h"

#include <errno.h>

static int xt_fs_option_bool(xt_value options, const char *key, int fallback) {
  if (!XT_IS_OBJECT(options)) return fallback;
  xt_value value = xt_object_get_cstr(options, key);
  if (XT_IS_UNDEFINED(value) || XT_IS_NULL(value)) return fallback;
  return xt_truthy(value);
}

static int xt_fs_option_recursive(xt_value options) {
  if (!xt_truthy(options)) return 0;
  if (XT_IS_OBJECT(options)) return xt_truthy(xt_object_get_cstr(options, "recursive"));
  return 1;
}

/* -- file contents --------------------------------------------------------- */

xt_value xt_node_p_read_file(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_READ_FILE);
  job->path = xt_fs_take_string(argv[0]);
  job->text = xt_fs_copy_cstr(argc > 1 ? xt_fs_encoding(argv[1]) : NULL);
  return xt_fs_job_submit(job);
}

static xt_value xt_fs_write(int32_t argc, xt_value *argv, int append) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_WRITE_FILE);
  job->path = xt_fs_take_string(argv[0]);
  job->flags = append;
  if (xt_node_is_buffer(argv[1])) {
    job->data = xt_node_buffer_bytes(argv[1], &job->data_length);
  } else {
    xt_value text = xt_to_string(argv[1]);
    const char *data = xt_string_data(text);
    int32_t length = xt_string_length_value(text);
    if (data && length >= 0) {
      const char *encoding = argc > 2 ? xt_fs_encoding(argv[2]) : NULL;
      job->data = xt_fs_decode_text(data, (size_t)length, encoding, &job->data_length);
    }
  }
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_write_file(int32_t argc, xt_value *argv) { return xt_fs_write(argc, argv, 0); }

xt_value xt_node_p_append_file(int32_t argc, xt_value *argv) { return xt_fs_write(argc, argv, 1); }

/* -- directories ----------------------------------------------------------- */

xt_value xt_node_p_mkdir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_MKDIR);
  job->path = xt_fs_take_string(argv[0]);
  job->mode = 0777;
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  if (XT_IS_OBJECT(options) && !XT_IS_UNDEFINED(xt_object_get_cstr(options, "mode"))) {
    job->mode = (int)xt_to_number(xt_object_get_cstr(options, "mode"));
  }
  job->recursive = argc > 1 && xt_fs_option_recursive(options);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_readdir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_READDIR);
  job->path = xt_fs_take_string(argv[0]);
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  job->with_types = xt_fs_option_bool(options, "withFileTypes", 0);
  job->recursive = xt_fs_option_bool(options, "recursive", 0);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_rm(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_RM);
  job->path = xt_fs_take_string(argv[0]);
  job->recursive = argc > 1 && xt_fs_option_recursive(argv[1]);
  job->force = xt_fs_option_bool(argc > 1 ? argv[1] : XT_UNDEFINED, "force", 0);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_unlink(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_UNLINK);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_rmdir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_RMDIR);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_rename(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_RENAME);
  job->path = xt_fs_take_string(argv[0]);
  job->path2 = xt_fs_take_string(argv[1]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_copy_file(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_COPY_FILE);
  job->path = xt_fs_take_string(argv[0]);
  job->path2 = xt_fs_take_string(argv[1]);
  job->flags = argc > 2 ? (int)xt_to_number(argv[2]) : 0;
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_cp(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_CP);
  job->path = xt_fs_take_string(argv[0]);
  job->path2 = xt_fs_take_string(argv[1]);
  xt_value options = argc > 2 ? argv[2] : XT_UNDEFINED;
  job->recursive = xt_fs_option_bool(options, "recursive", 0);
  job->force = xt_fs_option_bool(options, "force", 1);
  job->error_on_exist = xt_fs_option_bool(options, "errorOnExist", 0);
  job->dereference = xt_fs_option_bool(options, "dereference", 0);
  job->preserve = xt_fs_option_bool(options, "preserveTimestamps", 0);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_realpath(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_REALPATH);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

/* -- metadata -------------------------------------------------------------- */

static xt_value xt_fs_stat_job(xt_fs_job_kind kind, int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(kind);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_stat(int32_t argc, xt_value *argv) {
  return xt_fs_stat_job(XT_FS_JOB_STAT, argc, argv);
}

xt_value xt_node_p_lstat(int32_t argc, xt_value *argv) {
  return xt_fs_stat_job(XT_FS_JOB_LSTAT, argc, argv);
}

xt_value xt_node_p_statfs(int32_t argc, xt_value *argv) {
  return xt_fs_stat_job(XT_FS_JOB_STATFS, argc, argv);
}

xt_value xt_node_p_access(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_ACCESS);
  job->path = xt_fs_take_string(argv[0]);
  job->mode = argc > 1 ? (int)xt_to_number(argv[1]) : F_OK;
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_open(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_OPEN);
  job->path = xt_fs_take_string(argv[0]);
  job->flags = argc > 1 ? xt_fs_parse_flags(argv[1]) : (XT_FS_RDONLY | XT_FS_BINARY_FLAG);
  job->mode = argc > 2 ? (int)xt_to_number(argv[2]) : 0666;
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_chmod(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_CHMOD);
  job->path = xt_fs_take_string(argv[0]);
  job->mode = (int)xt_to_number(argv[1]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_lchmod(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_LCHMOD);
  job->path = xt_fs_take_string(argv[0]);
  job->mode = (int)xt_to_number(argv[1]);
  return xt_fs_job_submit(job);
}

static xt_value xt_fs_chown_job(xt_fs_job_kind kind, int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(kind);
  job->path = xt_fs_take_string(argv[0]);
  job->uid = (int)xt_to_number(argv[1]);
  job->gid = (int)xt_to_number(argv[2]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_chown(int32_t argc, xt_value *argv) {
  return xt_fs_chown_job(XT_FS_JOB_CHOWN, argc, argv);
}

xt_value xt_node_p_lchown(int32_t argc, xt_value *argv) {
  return xt_fs_chown_job(XT_FS_JOB_LCHOWN, argc, argv);
}

xt_value xt_node_p_truncate(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_TRUNCATE);
  job->path = xt_fs_take_string(argv[0]);
  job->length = argc > 1 ? (long long)xt_to_number(argv[1]) : 0;
  return xt_fs_job_submit(job);
}

static xt_value xt_fs_utimes_job(xt_fs_job_kind kind, int32_t argc, xt_value *argv) {
  if (argc < 3) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(kind);
  job->path = xt_fs_take_string(argv[0]);
  job->atime = xt_fs_time_seconds(argv[1]);
  job->mtime = xt_fs_time_seconds(argv[2]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_utimes(int32_t argc, xt_value *argv) {
  return xt_fs_utimes_job(XT_FS_JOB_UTIMES, argc, argv);
}

xt_value xt_node_p_lutimes(int32_t argc, xt_value *argv) {
  return xt_fs_utimes_job(XT_FS_JOB_LUTIMES, argc, argv);
}

/* -- links and temporary directories --------------------------------------- */

xt_value xt_node_p_link(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_LINK);
  job->path = xt_fs_take_string(argv[0]);
  job->path2 = xt_fs_take_string(argv[1]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_symlink(int32_t argc, xt_value *argv) {
  if (argc < 2) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_SYMLINK);
  job->path = xt_fs_take_string(argv[0]);
  job->path2 = xt_fs_take_string(argv[1]);
  job->text = argc > 2 ? xt_fs_take_string(argv[2]) : NULL;
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_readlink(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_READLINK);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_mkdtemp(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_MKDTEMP);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

/* -- opendir / glob / watch ------------------------------------------------ */

xt_value xt_node_p_opendir(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_OPENDIR);
  job->path = xt_fs_take_string(argv[0]);
  return xt_fs_job_submit(job);
}

xt_value xt_node_p_glob(int32_t argc, xt_value *argv) {
  if (argc < 1) return xt_promise_resolve(XT_UNDEFINED);
  xt_fs_job *job = xt_fs_job_new(XT_FS_JOB_GLOB);
  xt_value pattern = argv[0];
  if (XT_IS_ARRAY(pattern)) {
    int32_t count = (int32_t)xt_to_number(xt_array_length(pattern));
    if (count > 0) {
      job->patterns = (char **)calloc((size_t)count, sizeof(char *));
      if (!job->patterns) abort();
      for (int32_t i = 0; i < count; i++) {
        xt_value item = xt_array_get(pattern, xt_number((double)i));
        if (XT_IS_STRING(item)) {
          job->patterns[job->pattern_count++] = xt_fs_take_string(item);
        }
      }
    }
  } else {
    job->text = xt_fs_take_string(pattern);
  }
  xt_value options = argc > 1 ? argv[1] : XT_UNDEFINED;
  job->with_types = xt_fs_option_bool(options, "withFileTypes", 0);
  xt_value cwd = XT_IS_OBJECT(options) ? xt_object_get_cstr(options, "cwd") : XT_UNDEFINED;
  if (XT_IS_STRING(cwd)) job->path2 = xt_fs_take_string(cwd);
  return xt_fs_job_submit(job);
}

/* There is no real file-watching backend yet; keep the synchronous inert
 * watcher behind an already-settled promise. */
xt_value xt_node_p_watch(int32_t argc, xt_value *argv) {
  return xt_fs_promisify(xt_node_watch, argc, argv);
}
