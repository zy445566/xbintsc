/* -- Date ----------------------------------------------------------------- */

int xt_is_date(xt_value value) {
  return XT_IS_OBJECT(value) && ((xt_object *)XT_GET_PTR(value))->header.kind == XT_OBJECT_KIND_DATE;
}

/* Parse `YYYY-MM-DD`, optionally followed by `T`/space and
 * `HH:MM[:SS[.mmm]]`, then `Z`, `+HH:MM`/`-HH:MM` (or the HHMM spelling).
 * A date-only value is UTC; a date-time without an offset is local time, which
 * is what ECMAScript specifies. Returns milliseconds since the epoch, or NaN
 * for anything outside this subset. */
static int xt_date_digits(const char *text, int count, int *out) {
  int value = 0;
  for (int i = 0; i < count; i++) {
    if (text[i] < '0' || text[i] > '9') return 0;
    value = value * 10 + (text[i] - '0');
  }
  *out = value;
  return 1;
}

static double xt_date_parse_text(const char *text) {
  size_t length = strlen(text);
  size_t i = 0;
  int year = 0, month = 0, day = 0;
  if (length < 10 || !xt_date_digits(text, 4, &year) || text[4] != '-' ||
      !xt_date_digits(text + 5, 2, &month) || text[7] != '-' ||
      !xt_date_digits(text + 8, 2, &day)) {
    return NAN;
  }
  i = 10;
  int hour = 0, minute = 0, second = 0, millis = 0;
  int hasTime = 0;
  if (i < length && (text[i] == 'T' || text[i] == ' ')) {
    hasTime = 1;
    i++;
    if (i + 5 > length || !xt_date_digits(text + i, 2, &hour) || text[i + 2] != ':' ||
        !xt_date_digits(text + i + 3, 2, &minute)) {
      return NAN;
    }
    i += 5;
    if (i + 3 <= length && text[i] == ':') {
      if (!xt_date_digits(text + i + 1, 2, &second)) return NAN;
      i += 3;
    }
    if (i + 4 <= length && text[i] == '.') {
      if (!xt_date_digits(text + i + 1, 3, &millis)) return NAN;
      i += 4;
    }
  }
  int utc = !hasTime;
  int offsetSeconds = 0;
  if (i < length) {
    if (text[i] == 'Z') {
      utc = 1;
      i++;
    } else if (text[i] == '+' || text[i] == '-') {
      int sign = text[i] == '-' ? -1 : 1;
      i++;
      int offsetHour = 0, offsetMinute = 0;
      if (i + 5 <= length && xt_date_digits(text + i, 2, &offsetHour) && text[i + 2] == ':' &&
          xt_date_digits(text + i + 3, 2, &offsetMinute)) {
        i += 5;
      } else if (i + 4 <= length && xt_date_digits(text + i, 2, &offsetHour) &&
                 xt_date_digits(text + i + 2, 2, &offsetMinute)) {
        i += 4;
      } else {
        return NAN;
      }
      if (offsetHour > 23 || offsetMinute > 59) return NAN;
      offsetSeconds = sign * (offsetHour * 3600 + offsetMinute * 60);
      utc = 1;
    } else {
      return NAN;
    }
  }
  if (i != length) return NAN;
  if (month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59) {
    return NAN;
  }
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  time_t seconds;
  if (utc) {
    seconds = xt_timegm(&tm);
  } else {
    tm.tm_isdst = -1;
    seconds = mktime(&tm);
    if (seconds == (time_t)-1) return NAN;
  }
  if (utc && offsetSeconds != 0) seconds -= offsetSeconds;
  return (double)seconds * 1000.0 + (double)millis;
}

xt_value xt_date_ctor(int32_t argc, xt_value *argv) {
  xt_date *date = (xt_date *)xt_alloc(sizeof(xt_date), XT_OBJECT_KIND_DATE);
  if (argc == 0) date->time = (double)time(NULL) * 1000.0;
  else if (argc == 1) {
    if (XT_IS_STRING(argv[0])) {
      date->time = xt_date_parse_text(xt_string_data(argv[0]));
    } else {
      date->time = xt_to_number(argv[0]);
    }
  } else {
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
  if (strcmp(fn, "parse") == 0) {
    xt_value input = xt_arg_at(argc, argv, 0);
    if (!XT_IS_STRING(input)) return xt_number(NAN);
    return xt_number(xt_date_parse_text(xt_string_data(input)));
  }
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
  (void)argc;
  (void)argv;
  *handled = 1;
  xt_date *date = (xt_date *)XT_GET_PTR(target);
  if (strcmp(method, "getTime") == 0 || strcmp(method, "valueOf") == 0) return xt_number(date->time);
  time_t seconds = (time_t)(date->time / 1000.0);
  struct tm tm;
  const char *name = method;
  char utc[32];
  int isUtc = 0;
  if (strncmp(method, "getUTC", 6) == 0) {
    snprintf(utc, sizeof(utc), "get%s", method + 6);
    name = utc;
    isUtc = 1;
  }
  /* Bare getters and `toString` report local time; `getUTC*` and
   * `toISOString` report UTC. */
  if (isUtc || strcmp(method, "toISOString") == 0) xt_gmtime_r(&seconds, &tm);
  else xt_localtime_r(&seconds, &tm);
  if (strcmp(name, "getFullYear") == 0) return xt_number(tm.tm_year + 1900);
  if (strcmp(name, "getMonth") == 0) return xt_number(tm.tm_mon);
  if (strcmp(name, "getDate") == 0) return xt_number(tm.tm_mday);
  if (strcmp(name, "getDay") == 0) return xt_number(tm.tm_wday);
  if (strcmp(name, "getHours") == 0) return xt_number(tm.tm_hour);
  if (strcmp(name, "getMinutes") == 0) return xt_number(tm.tm_min);
  if (strcmp(name, "getSeconds") == 0) return xt_number(tm.tm_sec);
  if (strcmp(name, "getMilliseconds") == 0) return xt_number(fmod(date->time, 1000.0));
  if (strcmp(method, "toISOString") == 0) {
    if (isnan(date->time)) {
      xt_value message = xt_string_from_cstr("Invalid time value");
      xt_throw(xt_range_error_ctor(1, &message));
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)fmod(date->time, 1000.0));
    return xt_string_from_cstr(buffer);
  }
  if (strcmp(method, "toJSON") == 0 || strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("toISOString"), 0, NULL);
  *handled = 0;
  return XT_UNDEFINED;
}
