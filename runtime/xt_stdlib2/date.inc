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
  if (argc == 0) date->time = xt_wallclock_ms();
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
  if (strcmp(fn, "now") == 0) return xt_number(xt_wallclock_ms());
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
    /* Date.UTC(year, month[, day[, hour[, minute[, second[, ms]]]]]). The time
     * fields used to be dropped, so every UTC date came back at midnight. */
    tm.tm_hour = argc > 3 ? xt_to_int32(xt_arg_at(argc, argv, 3)) : 0;
    tm.tm_min = argc > 4 ? xt_to_int32(xt_arg_at(argc, argv, 4)) : 0;
    tm.tm_sec = argc > 5 ? xt_to_int32(xt_arg_at(argc, argv, 5)) : 0;
    double millis = argc > 6 ? xt_to_int32(xt_arg_at(argc, argv, 6)) : 0;
    return xt_number((double)xt_timegm(&tm) * 1000.0 + millis);
  }
  return XT_UNDEFINED;
}

static const char *const xt_date_month_long[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December",
};
static const char *const xt_date_month_short[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};
static const char *const xt_date_weekday_long[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};
static const char *const xt_date_weekday_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void xt_date_append(char *out, size_t capacity, size_t *length, const char *text) {
  if (text == NULL) return;
  size_t count = strlen(text);
  if (*length + count + 1 > capacity) count = capacity > *length + 1 ? capacity - *length - 1 : 0;
  memcpy(out + *length, text, count);
  *length += count;
  out[*length] = '\0';
}

static void xt_date_append_int(char *out, size_t capacity, size_t *length, int value, int width) {
  char buffer[24];
  if (width == 2) {
    snprintf(buffer, sizeof(buffer), "%02d", value);
  } else {
    snprintf(buffer, sizeof(buffer), "%d", value);
  }
  xt_date_append(out, capacity, length, buffer);
}

/** Read a string option (`{ month: "long" }`); null when absent. */
static const char *xt_date_option(xt_value options, const char *name) {
  if (!XT_IS_OBJECT(options)) return NULL;
  xt_value value = xt_object_get_cstr(options, name);
  if (XT_IS_UNDEFINED(value) || XT_IS_NULL(value)) return NULL;
  return xt_string_data(xt_to_string(value));
}

static int xt_date_option_width(const char *value) {
  return value != NULL && strcmp(value, "2-digit") == 0 ? 2 : 1;
}

static int xt_date_has_option(xt_value options, const char *const *names, int count) {
  for (int i = 0; i < count; i++) {
    if (xt_date_option(options, names[i]) != NULL) return 1;
  }
  return 0;
}

/** `toLocaleDateString` with a small, deterministic en-US-style subset of the
 * options object. No locale database is linked, so unknown locales still use
 * the same month/day order. */
static xt_value xt_date_locale_date(const struct tm *tm, xt_value options) {
  static const char *const names[] = {"weekday", "year", "month", "day"};
  int custom = xt_date_has_option(options, names, 4);
  char buffer[128];
  size_t length = 0;
  buffer[0] = '\0';
  if (!custom) {
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_mon + 1, 1);
    xt_date_append(buffer, sizeof(buffer), &length, "/");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_mday, 1);
    xt_date_append(buffer, sizeof(buffer), &length, "/");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_year + 1900, 4);
    return xt_string_from_cstr(buffer);
  }
  const char *weekday = xt_date_option(options, "weekday");
  if (weekday != NULL) {
    if (strcmp(weekday, "long") == 0) xt_date_append(buffer, sizeof(buffer), &length,
                                                    xt_date_weekday_long[tm->tm_wday]);
    else xt_date_append(buffer, sizeof(buffer), &length, xt_date_weekday_short[tm->tm_wday]);
    xt_date_append(buffer, sizeof(buffer), &length, ", ");
  }
  const char *month = xt_date_option(options, "month");
  const char *day = xt_date_option(options, "day");
  const char *year = xt_date_option(options, "year");
  if (month != NULL && (strcmp(month, "long") == 0 || strcmp(month, "short") == 0 ||
                        strcmp(month, "narrow") == 0)) {
    if (strcmp(month, "long") == 0) {
      xt_date_append(buffer, sizeof(buffer), &length, xt_date_month_long[tm->tm_mon]);
    } else {
      xt_date_append(buffer, sizeof(buffer), &length, xt_date_month_short[tm->tm_mon]);
    }
    if (day != NULL) {
      xt_date_append(buffer, sizeof(buffer), &length, " ");
      xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_mday,
                         xt_date_option_width(day));
    }
    if (year != NULL) {
      xt_date_append(buffer, sizeof(buffer), &length, ", ");
      xt_date_append_int(buffer, sizeof(buffer), &length,
                         xt_date_option_width(year) == 2 ? (tm->tm_year + 1900) % 100
                                                          : tm->tm_year + 1900,
                         xt_date_option_width(year));
    }
    return xt_string_from_cstr(buffer);
  }
  if (month != NULL) {
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_mon + 1,
                       xt_date_option_width(month));
  }
  if (day != NULL) {
    if (month != NULL) xt_date_append(buffer, sizeof(buffer), &length, "/");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_mday, xt_date_option_width(day));
  }
  if (year != NULL) {
    if (month != NULL || day != NULL) xt_date_append(buffer, sizeof(buffer), &length, "/");
    xt_date_append_int(buffer, sizeof(buffer), &length,
                       xt_date_option_width(year) == 2 ? (tm->tm_year + 1900) % 100
                                                        : tm->tm_year + 1900,
                       xt_date_option_width(year));
  }
  return xt_string_from_cstr(buffer);
}

static xt_value xt_date_locale_time(const struct tm *tm, xt_value options) {
  static const char *const names[] = {"hour", "minute", "second"};
  int custom = xt_date_has_option(options, names, 3);
  const char *hour12_option = xt_date_option(options, "hour12");
  int hour12 = 1;
  if (hour12_option != NULL) hour12 = strcmp(hour12_option, "false") != 0;
  int hour = tm->tm_hour;
  const char *suffix = hour >= 12 ? "PM" : "AM";
  if (hour12) hour = hour % 12 == 0 ? 12 : hour % 12;
  char buffer[64];
  size_t length = 0;
  buffer[0] = '\0';
  const char *hour_option = xt_date_option(options, "hour");
  const char *minute_option = xt_date_option(options, "minute");
  const char *second_option = xt_date_option(options, "second");
  if (!custom) {
    xt_date_append_int(buffer, sizeof(buffer), &length, hour, 1);
    xt_date_append(buffer, sizeof(buffer), &length, ":");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_min, 2);
    xt_date_append(buffer, sizeof(buffer), &length, ":");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_sec, 2);
    if (hour12) {
      xt_date_append(buffer, sizeof(buffer), &length, " ");
      xt_date_append(buffer, sizeof(buffer), &length, suffix);
    }
    return xt_string_from_cstr(buffer);
  }
  if (hour_option != NULL) {
    xt_date_append_int(buffer, sizeof(buffer), &length, hour, xt_date_option_width(hour_option));
  } else if (minute_option != NULL || second_option != NULL) {
    xt_date_append_int(buffer, sizeof(buffer), &length, hour, 1);
  }
  if (minute_option != NULL) {
    xt_date_append(buffer, sizeof(buffer), &length, ":");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_min,
                       xt_date_option_width(minute_option));
  }
  if (second_option != NULL) {
    xt_date_append(buffer, sizeof(buffer), &length, ":");
    xt_date_append_int(buffer, sizeof(buffer), &length, tm->tm_sec,
                       xt_date_option_width(second_option));
  }
  if (hour12) {
    xt_date_append(buffer, sizeof(buffer), &length, " ");
    xt_date_append(buffer, sizeof(buffer), &length, suffix);
  }
  return xt_string_from_cstr(buffer);
}

static xt_value xt_date_method(xt_value target, const char *method, int32_t argc, xt_value *argv, int *handled) {
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
  if (isUtc || strcmp(method, "toISOString") == 0 || strcmp(method, "toUTCString") == 0) {
    xt_gmtime_r(&seconds, &tm);
  } else {
    xt_localtime_r(&seconds, &tm);
  }
  /* Locale/time-string methods answer "Invalid Date" for NaN time values, as
   * browsers do; `toISOString` is the one that throws. */
  if (strcmp(method, "toLocaleDateString") == 0 || strcmp(method, "toLocaleTimeString") == 0 ||
      strcmp(method, "toLocaleString") == 0 || strcmp(method, "toDateString") == 0 ||
      strcmp(method, "toTimeString") == 0 || strcmp(method, "toUTCString") == 0) {
    if (isnan(date->time)) return xt_string_from_cstr("Invalid Date");
    xt_value options = XT_UNDEFINED;
    if (argc >= 2 && XT_IS_OBJECT(argv[1])) options = argv[1];
    else if (argc >= 1 && XT_IS_OBJECT(argv[0])) options = argv[0];
    if (strcmp(method, "toDateString") == 0) {
      char buffer[48];
      snprintf(buffer, sizeof(buffer), "%s %s %02d %04d", xt_date_weekday_short[tm.tm_wday],
               xt_date_month_short[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
      return xt_string_from_cstr(buffer);
    }
    if (strcmp(method, "toTimeString") == 0) {
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
      return xt_string_from_cstr(buffer);
    }
    if (strcmp(method, "toUTCString") == 0) {
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT",
               xt_date_weekday_short[tm.tm_wday], tm.tm_mday, xt_date_month_short[tm.tm_mon],
               tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
      return xt_string_from_cstr(buffer);
    }
    xt_value date_part = xt_date_locale_date(&tm, options);
    if (strcmp(method, "toLocaleDateString") == 0) return date_part;
    xt_value time_part = xt_date_locale_time(&tm, options);
    if (strcmp(method, "toLocaleTimeString") == 0) return time_part;
    xt_value comma = xt_string_from_cstr(", ");
    return xt_add(xt_add(date_part, comma), time_part);
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
    if (isnan(date->time)) {
      xt_value message = xt_string_from_cstr("Invalid time value");
      xt_throw(xt_range_error_ctor(1, &message));
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)fmod(date->time, 1000.0));
    return xt_string_from_cstr(buffer);
  }
  /* `toJSON` is the JSON hook: an invalid date serializes as `null` instead of
   * throwing the way `toISOString` does. */
  if (strcmp(method, "toJSON") == 0) {
    if (isnan(date->time)) return XT_NULL;
    return xt_call_method(target, xt_string_from_cstr("toISOString"), 0, NULL);
  }
  if (strcmp(method, "toString") == 0) return xt_call_method(target, xt_string_from_cstr("toISOString"), 0, NULL);
  *handled = 0;
  return XT_UNDEFINED;
}
