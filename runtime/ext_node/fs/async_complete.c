/*
 * Main-thread half of `fs/promises`: turn worker results into values.
 *
 * Runs on the reactor thread, so `xt_value` construction, promise settlement
 * and the collector are all safe here. Everything the job owns is freed by
 * `xt_fs_job_release` after this returns (except the entry table handed over to
 * a `Dir` object).
 */

#include "async_job.h"

static void xt_fs_job_resolve(xt_fs_job *job, xt_value value) {
  xt_promise_fulfill(job->base.roots[0], value);
}

static void xt_fs_job_reject(xt_fs_job *job) {
  xt_value error = xt_fs_error_object(job->err, job->syscall, job->error_path);
  xt_promise_reject_value(job->base.roots[0], error);
}

static xt_value xt_fs_statfs_value(const long long *numbers) {
  xt_value object = xt_object_new();
  xt_object_set(object, xt_string_from_cstr("type"), xt_number((double)numbers[0]));
  xt_object_set(object, xt_string_from_cstr("bsize"), xt_number((double)numbers[1]));
  xt_object_set(object, xt_string_from_cstr("blocks"), xt_number((double)numbers[2]));
  xt_object_set(object, xt_string_from_cstr("bfree"), xt_number((double)numbers[3]));
  xt_object_set(object, xt_string_from_cstr("bavail"), xt_number((double)numbers[4]));
  xt_object_set(object, xt_string_from_cstr("files"), xt_number((double)numbers[5]));
  xt_object_set(object, xt_string_from_cstr("ffree"), xt_number((double)numbers[6]));
  return object;
}

static xt_value xt_fs_readdir_value(xt_fs_job *job) {
  xt_value list = xt_array_new(0, NULL);
  for (size_t i = 0; i < job->entry_count; i++) {
    xt_fs_entry *entry = &job->entries[i];
    if (job->with_types) {
      xt_array_push(list, xt_node_dirent_named(entry->bare ? entry->bare : entry->name, entry->mode));
    } else {
      xt_array_push(list, xt_string_from_cstr(entry->name));
    }
  }
  return list;
}

static xt_value xt_fs_glob_value(xt_fs_job *job) {
  xt_value list = xt_array_new(0, NULL);
  for (size_t i = 0; i < job->entry_count; i++) {
    xt_array_push(list, xt_string_from_cstr(job->entries[i].name));
  }
  return list;
}

static xt_value xt_fs_read_bytes_value(xt_fs_job *job) {
  return xt_fs_encode_bytes(job->bytes, job->bytes_length, job->text);
}

static void xt_fs_fd_read_complete(xt_fs_job *job) {
  xt_value buffer = job->base.roots[1];
  uint32_t offset = (uint32_t)job->offset;
  for (size_t i = 0; i < job->bytes_length; i++) {
    xt_node_buffer_set(buffer, offset + (uint32_t)i, job->bytes[i]);
  }
  xt_fs_job_resolve(job, xt_number((double)job->bytes_length));
}

void xt_fs_job_complete(xt_async_job *base) {
  xt_fs_job *job = (xt_fs_job *)base;
  if (job->err) {
    xt_fs_job_reject(job);
    return;
  }
  switch (job->kind) {
    case XT_FS_JOB_READ_FILE:
    case XT_FS_JOB_FD_READ_FILE: xt_fs_job_resolve(job, xt_fs_read_bytes_value(job)); break;
    case XT_FS_JOB_READDIR: xt_fs_job_resolve(job, xt_fs_readdir_value(job)); break;
    case XT_FS_JOB_GLOB: xt_fs_job_resolve(job, xt_fs_glob_value(job)); break;
    case XT_FS_JOB_OPENDIR: {
      xt_value dir = xt_node_dir_from_entries(job->path, job->entries, job->entry_count);
      job->entries = NULL;
      job->entry_count = 0;
      xt_fs_job_resolve(job, dir);
      break;
    }
    case XT_FS_JOB_REALPATH:
    case XT_FS_JOB_READLINK:
    case XT_FS_JOB_MKDTEMP:
      xt_fs_job_resolve(job, job->string ? xt_string_from_cstr(job->string) : XT_UNDEFINED);
      break;
    case XT_FS_JOB_STAT:
    case XT_FS_JOB_LSTAT:
    case XT_FS_JOB_FD_STAT: xt_fs_job_resolve(job, xt_node_stat_result(&job->stat)); break;
    case XT_FS_JOB_STATFS: xt_fs_job_resolve(job, xt_fs_statfs_value(job->numbers)); break;
    case XT_FS_JOB_OPEN: xt_fs_job_resolve(job, xt_fh_new(xt_number((double)job->result_fd))); break;
    case XT_FS_JOB_FD_READ: xt_fs_fd_read_complete(job); break;
    case XT_FS_JOB_FD_WRITE: xt_fs_job_resolve(job, xt_number((double)job->written)); break;
    default: xt_fs_job_resolve(job, XT_UNDEFINED); break;
  }
}
