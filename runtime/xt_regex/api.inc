/* -- public API ----------------------------------------------------------- */

static int regcomp(regex_t *preg, const char *pattern, int cflags) {
  xt_re_parser p;
  xt_re_alt *root;
  if (!preg || !pattern) return 1;
  memset(preg, 0, sizeof(*preg));
  p.s = pattern;
  p.i = 0;
  p.error = 0;
  p.groupCount = 0;
  root = xt_re_parse_alt(&p);
  if (!root || p.error || p.s[p.i] != '\0') {
    if (root) xt_re_free_alt(root);
    return 1;
  }
  preg->root = root;
  preg->icase = (cflags & REG_ICASE) ? 1 : 0;
  return 0;
}

static int regexec(const regex_t *preg, const char *string, size_t nmatch,
                   regmatch_t pmatch[], int eflags) {
  xt_re_input in;
  size_t start;
  size_t i;
  (void)eflags;
  if (!preg || !preg->root || !string) return 1;
  if (pmatch) {
    for (i = 0; i < nmatch; i++) {
      pmatch[i].rm_so = -1;
      pmatch[i].rm_eo = -1;
    }
  }
  in.text = string;
  in.len = strlen(string);
  in.icase = preg->icase;
  for (start = 0; start <= in.len; start++) {
    xt_re_match_result result;
    result.end = 0;
    result.pmatch = pmatch;
    result.nmatch = nmatch;
    if (xt_re_match_alt(preg->root, &in, start, xt_re_record_match, &result, NULL)) {
      if (nmatch > 0 && pmatch) {
        pmatch[0].rm_so = (int)start;
        pmatch[0].rm_eo = (int)result.end;
      }
      return 0;
    }
  }
  return 1;
}

static void regfree(regex_t *preg) {
  if (!preg) return;
  xt_re_free_alt(preg->root);
  preg->root = NULL;
}
