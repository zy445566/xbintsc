/* -- Date ----------------------------------------------------------------- */

int xt_is_date(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_DATE;
}

xt_value xt_date_ctor(int32_t argc, xt_value *argv) {
  xt_date *date = (xt_date *)xt_alloc(sizeof(xt_date), XT_OBJECT_KIND_DATE);
  if (argc == 0) date->time = (double)time(NULL) * 1000.0;
  else if (argc == 1) date->time = XT_IS_STRING(argv[0]) ? 0.0 : xt_to_number(argv[0]);
  else {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = xt_to_int32(argv[0]) - 1900;
    tm.tm_mon = xt_to_int32(argv[1]);
    tm.tm_mday = argc > 2 ? xt_to_int32(argv[2]) : 1;
    tm.tm_hour = argc > 3 ? xt_to_int32(argv[3]) : 0;
    tm.tm_min = argc > 4 ? xt_to_int32(argv[4]) : 0;
    tm.tm_sec = argc > 5 ? xt_to_int32(argv[5]) : 0;
    tm.tm_isdst = -1;
    date->time = (double)mktime(&tm) * 1000.0;
  }
  return XT_FROM_PTR(XT_TAG_OBJECT, date);
}

xt_value xt_date_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "now") == 0) return xt_number((double)time(NULL) * 1000.0);
  if (strcmp(fn, "parse") == 0) return xt_number(0);
  if (strcmp(fn, "UTC") == 0) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = xt_to_int32(xt_arg_at(argc, argv, 0)) - 1900;
    tm.tm_mon = xt_to_int32(xt_arg_at(argc, argv, 1));
    tm.tm_mday = argc > 2 ? xt_to_int32(xt_arg_at(argc, argv, 2)) : 1;
    return xt_number((double)xt_timegm(&tm) * 1000.0);
  }
  return XT_UNDEFINED;
}

static xt_value xt_date_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
  *handled = 1;
  xt_date *date = (xt_date *)XT_GET_PTR(target);
  time_t seconds = (time_t)(date->time / 1000.0);
  struct tm tm;
  xt_gmtime_r(&seconds, &tm);
  if (strcmp(method, "getTime") == 0 || strcmp(method, "valueOf") == 0) return xt_number(date->time);
  const char *name = method;
  char utc[32];
  if (strncmp(method, "getUTC", 6) == 0) {
    snprintf(utc, sizeof(utc), "get%s", method + 6);
    name = utc;
  }
  if (strcmp(name, "getFullYear") == 0) return xt_number(tm.tm_year + 1900);
  if (strcmp(name, "getMonth") == 0) return xt_number(tm.tm_mon);
  if (strcmp(name, "getDate") == 0) return xt_number(tm.tm_mday);
  if (strcmp(name, "getDay") == 0) return xt_number(tm.tm_wday);
  if (strcmp(name, "getHours") == 0) return xt_number(tm.tm_hour);
  if (strcmp(name, "getMinutes") == 0) return xt_number(tm.tm_min);
  if (strcmp(name, "getSeconds") == 0) return xt_number(tm.tm_sec);
  if (strcmp(name, "getMilliseconds") == 0) return xt_number(fmod(date->time, 1000.0));
  if (strcmp(method, "toISOString") == 0) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)fmod(date->time, 1000.0));
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toJSON") == 0 || strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("toISOString"), 0, NULL);
  *handled = 0;
  return XT_UNDEFINED;
}
