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
  if (index >= 0) xt_console_timers[index] = clock();
  return XT_UNDEFINED;
}

xt_value xt_console_time_log(int32_t argc, xt_value *argv) {
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0 && xt_console_timers[index] != 0) {
    double ms = (double)(clock() - xt_console_timers[index]) * 1000.0 / CLOCKS_PER_SEC;
    printf("%s: %.3fms\n", xt_console_labels[index], ms);
  }
  return XT_UNDEFINED;
}

xt_value xt_console_time_end(int32_t argc, xt_value *argv) {
  xt_console_time_log(argc, argv);
  const char *label = xt_console_label(argc, argv);
  int index = xt_console_find_label(label ? label : "default");
  if (index >= 0) xt_console_timers[index] = 0;
  return XT_UNDEFINED;
}
