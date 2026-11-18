/* ------------------------------------------------------------------------- */
/* Console methods                                                           */
/* ------------------------------------------------------------------------- */

void xt_console_info(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stdout); }
void xt_console_warn(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }
void xt_console_error(int32_t argc, xt_value *argv) { xt_console_write(argc, argv, stderr); }

xt_value xt_console_dir(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  return XT_UNDEFINED;
}

xt_value xt_console_trace(int32_t argc, xt_value *argv) {
  fputs("Trace", stderr);
  for (int32_t i = 0; i < argc; i++) {
    fputc(' ', stderr);
    xt_inspect_top(argv[i], stderr);
  }
  fputc('\n', stderr);
  return XT_UNDEFINED;
}

xt_value xt_console_assert(int32_t argc, xt_value *argv) {
  if (argc == 0 || !xt_truthy(argv[0])) {
    fputs("Assertion failed", stderr);
    if (argc > 1) fputs(":", stderr);
    for (int32_t i = 1; i < argc; i++) {
      fputc(' ', stderr);
      xt_inspect_top(argv[i], stderr);
    }
    fputc('\n', stderr);
  }
  return XT_UNDEFINED;
}

#define XT_CONSOLE_MAX_LABELS 32
static char *xt_console_labels[XT_CONSOLE_MAX_LABELS];
static long xt_console_counts[XT_CONSOLE_MAX_LABELS];
static clock_t xt_console_timers[XT_CONSOLE_MAX_LABELS];
/* A running timer is tracked by its own flag: clock() legitimately returns 0
   (notably on Windows, where its resolution is one millisecond and a process
   started less than a millisecond ago reports 0), so the timestamp itself
   cannot serve as the "no timer running" sentinel. */
static unsigned char xt_console_timer_active[XT_CONSOLE_MAX_LABELS];
static int xt_console_label_count = 0;
static int xt_console_depth = 0;

static int xt_console_find_label(const char *label) {
  for (int i = 0; i < xt_console_label_count; i++) {
    if (strcmp(xt_console_labels[i], label) == 0) return i;
  }
  if (xt_console_label_count >= XT_CONSOLE_MAX_LABELS) return -1;
  int index = xt_console_label_count++;
  xt_console_labels[index] = (char *)malloc(strlen(label) + 1);
  memcpy(xt_console_labels[index], label, strlen(label) + 1);
  xt_console_counts[index] = 0;
  xt_console_timers[index] = 0;
  xt_console_timer_active[index] = 0;
  return index;
}

static const char *xt_console_label(int32_t argc, xt_value *argv) {
  if (argc == 0) return "default";
  return xt_string_data(xt_to_string(argv[0]));
}

xt_value xt_console_count(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index < 0) return XT_UNDEFINED;
  xt_console_counts[index]++;
  printf("%s: %ld\n", xt_console_labels[index], xt_console_counts[index]);
  return XT_UNDEFINED;
}

xt_value xt_console_count_reset(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) xt_console_counts[index] = 0;
  return XT_UNDEFINED;
}

xt_value xt_console_group(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  xt_console_depth++;
  return XT_UNDEFINED;
}

xt_value xt_console_group_end(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (xt_console_depth > 0) xt_console_depth--;
  return XT_UNDEFINED;
}

xt_value xt_console_table(int32_t argc, xt_value *argv) {
  xt_console_write(argc, argv, stdout);
  return XT_UNDEFINED;
}

xt_value xt_console_time(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) {
    xt_console_timers[index] = clock();
    xt_console_timer_active[index] = 1;
  }
  return XT_UNDEFINED;
}

xt_value xt_console_time_log(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0 && xt_console_timer_active[index]) {
    double ms = (double)(clock() - xt_console_timers[index]) * 1000.0 / CLOCKS_PER_SEC;
    printf("%s: %.3fms\n", xt_console_labels[index], ms);
  }
  return XT_UNDEFINED;
}

xt_value xt_console_time_end(int32_t argc, xt_value *argv) {
  xt_console_time_log(argc, argv);
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) {
    xt_console_timers[index] = 0;
    xt_console_timer_active[index] = 0;
  }
  return XT_UNDEFINED;
}

/* ------------------------------------------------------------------------- */
/* The global `console` object                                               */
/* ------------------------------------------------------------------------- */

/*
 * `console` is a global *value* as well as a namespace: code writes
 * `const log = console.log`, `console.error.bind(console)` and
 * `const { warn } = console`, so the binding has to be a real object whose
 * methods are ordinary first-class functions. The object is built once and
 * rooted; each method is a closure over the raw `(argc, argv)` runtime symbol,
 * so a method keeps its behaviour when detached from the object.
 */

typedef void (*xt_console_void_method)(int32_t, xt_value *);
typedef xt_value (*xt_console_value_method)(int32_t, xt_value *);

static xt_value xt_console_void_bridge(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  ((xt_console_void_method)(uintptr_t)xt_closure_env(env, 0))(argc, argv);
  return XT_UNDEFINED;
}

static xt_value xt_console_value_bridge(xt_value thisValue, xt_value env, int32_t argc,
                                        xt_value *argv) {
  (void)thisValue;
  return ((xt_console_value_method)(uintptr_t)xt_closure_env(env, 0))(argc, argv);
}

static void xt_console_add(xt_value object, const char *name, void *method, int returnsValue) {
  xt_value env[1];
  env[0] = (xt_value)(uintptr_t)method;
  xt_value closure = xt_closure_new(
      returnsValue ? (void *)xt_console_value_bridge : (void *)xt_console_void_bridge, 1, env);
  xt_value key = xt_string_from_cstr(name);
  xt_function_set_metadata(closure, key, returnsValue ? 1 : 0);
  xt_object_set(object, key, closure);
}

xt_value xt_console_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  static xt_value cached = 0;
  if (cached != 0) return cached;
  xt_value object = xt_object_new();
  xt_console_add(object, "log", (void *)xt_console_log, 0);
  xt_console_add(object, "info", (void *)xt_console_info, 0);
  xt_console_add(object, "warn", (void *)xt_console_warn, 0);
  xt_console_add(object, "error", (void *)xt_console_error, 0);
  xt_console_add(object, "debug", (void *)xt_console_log, 0);
  xt_console_add(object, "dir", (void *)xt_console_dir, 1);
  xt_console_add(object, "trace", (void *)xt_console_trace, 1);
  xt_console_add(object, "assert", (void *)xt_console_assert, 1);
  xt_console_add(object, "count", (void *)xt_console_count, 1);
  xt_console_add(object, "countReset", (void *)xt_console_count_reset, 1);
  xt_console_add(object, "group", (void *)xt_console_group, 1);
  xt_console_add(object, "groupEnd", (void *)xt_console_group_end, 1);
  xt_console_add(object, "table", (void *)xt_console_table, 1);
  xt_console_add(object, "time", (void *)xt_console_time, 1);
  xt_console_add(object, "timeEnd", (void *)xt_console_time_end, 1);
  xt_console_add(object, "timeLog", (void *)xt_console_time_log, 1);
  cached = object;
  xt_gc_add_root(&cached);
  return object;
}
