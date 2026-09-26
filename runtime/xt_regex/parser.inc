/* -- parser --------------------------------------------------------------- */

typedef struct {
  const char *s;
  size_t i;
  int error;
  int groupCount;
} xt_re_parser;

static void xt_re_free_alt(xt_re_alt *alt);

static xt_re_node *xt_re_new_node(int type) {
  xt_re_node *n = (xt_re_node *)calloc(1, sizeof(xt_re_node));
  if (n) n->type = type;
  return n;
}

static xt_re_node *xt_re_new_char(unsigned char c) {
  xt_re_node *n = xt_re_new_node(XT_RE_CHAR);
  if (n) n->ch = c;
  return n;
}

static xt_re_node *xt_re_new_class(const unsigned char *bits) {
  xt_re_node *n = xt_re_new_node(XT_RE_CLASS);
  if (!n) return NULL;
  n->classbits = (unsigned char *)malloc(32);
  if (!n->classbits) {
    free(n);
    return NULL;
  }
  memcpy(n->classbits, bits, 32);
  return n;
}

static void xt_re_push_piece(xt_re_concat *c, xt_re_node *node, int quant) {
  if (c->count == c->capacity) {
    int next = c->capacity ? c->capacity * 2 : 4;
    xt_re_piece *grown = (xt_re_piece *)realloc(c->pieces, (size_t)next * sizeof(xt_re_piece));
    if (!grown) return;
    c->pieces = grown;
    c->capacity = next;
  }
  c->pieces[c->count].node = node;
  c->pieces[c->count].quant = quant;
  c->count++;
}

static void xt_re_push_branch(xt_re_alt *alt, xt_re_concat c) {
  if (alt->count == alt->capacity) {
    int next = alt->capacity ? alt->capacity * 2 : 4;
    xt_re_concat *grown = (xt_re_concat *)realloc(alt->branches, (size_t)next * sizeof(xt_re_concat));
    if (!grown) return;
    alt->branches = grown;
    alt->capacity = next;
  }
  alt->branches[alt->count++] = c;
}

static xt_re_alt *xt_re_parse_alt(xt_re_parser *p);

static xt_re_node *xt_re_parse_escape(xt_re_parser *p) {
  char c;
  p->i++; /* consume backslash */
  c = p->s[p->i];
  if (!c) {
    p->error = 1;
    return NULL;
  }
  p->i++;
  if (c == 'd' || c == 'D' || c == 'w' || c == 'W' || c == 's' || c == 'S') {
    unsigned char bits[32];
    xt_re_node *n;
    memset(bits, 0, sizeof(bits));
    xt_re_add_predefined(bits, c);
    n = xt_re_new_class(bits);
    if (!n) p->error = 1;
    return n;
  }
  if (c == 'n') return xt_re_new_char('\n');
  if (c == 't') return xt_re_new_char('\t');
  if (c == 'r') return xt_re_new_char('\r');
  if (c == 'f') return xt_re_new_char('\f');
  if (c == 'v') return xt_re_new_char('\v');
  if (c == '0') return xt_re_new_char('\0');
  return xt_re_new_char((unsigned char)c);
}

static xt_re_node *xt_re_parse_class(xt_re_parser *p) {
  unsigned char bits[32];
  int negate = 0;
  int first = 1;
  xt_re_node *n;

  memset(bits, 0, sizeof(bits));
  p->i++; /* consume '[' */
  if (p->s[p->i] == '^') {
    negate = 1;
    p->i++;
  }
  while (p->s[p->i] && (p->s[p->i] != ']' || first)) {
    unsigned char lo;
    first = 0;
    if (p->s[p->i] == '\\' && p->s[p->i + 1]) {
      char e;
      p->i++;
      e = p->s[p->i++];
      if (xt_re_add_predefined(bits, e)) continue;
      if (e == 'n') lo = '\n';
      else if (e == 't') lo = '\t';
      else if (e == 'r') lo = '\r';
      else if (e == 'f') lo = '\f';
      else if (e == 'v') lo = '\v';
      else lo = (unsigned char)e;
    } else {
      lo = (unsigned char)p->s[p->i++];
    }
    if (p->s[p->i] == '-' && p->s[p->i + 1] && p->s[p->i + 1] != ']') {
      unsigned char hi;
      p->i++; /* consume '-' */
      hi = (unsigned char)p->s[p->i++];
      xt_re_add_range(bits, lo, hi);
    } else {
      xt_re_set_bit(bits, lo);
    }
  }
  if (p->s[p->i] != ']') {
    p->error = 1;
    return NULL;
  }
  p->i++;
  if (negate) {
    int c;
    for (c = 0; c < 32; c++) bits[c] = (unsigned char)~bits[c];
  }
  n = xt_re_new_class(bits);
  if (!n) p->error = 1;
  return n;
}

static xt_re_node *xt_re_parse_atom(xt_re_parser *p) {
  char c = p->s[p->i];
  if (c == '(') {
    xt_re_node *n;
    xt_re_alt *group;
    int index;
    p->i++;
    /* Number the group as soon as its '(' is seen so nested groups keep
     * POSIX/JavaScript numbering (opening-paren order). */
    index = ++p->groupCount;
    group = xt_re_parse_alt(p);
    if (p->error) return NULL;
    if (p->s[p->i] != ')') {
      p->error = 1;
      xt_re_free_alt(group);
      return NULL;
    }
    p->i++;
    n = xt_re_new_node(XT_RE_GROUP);
    if (!n) {
      p->error = 1;
      return NULL;
    }
    n->group = group;
    n->groupIndex = index;
    return n;
  }
  if (c == '[') return xt_re_parse_class(p);
  if (c == '.') {
    p->i++;
    return xt_re_new_node(XT_RE_ANY);
  }
  if (c == '^') {
    p->i++;
    return xt_re_new_node(XT_RE_BOL);
  }
  if (c == '$') {
    p->i++;
    return xt_re_new_node(XT_RE_EOL);
  }
  if (c == '\\') return xt_re_parse_escape(p);
  if (c == '\0') {
    p->error = 1;
    return NULL;
  }
  p->i++;
  return xt_re_new_char((unsigned char)c);
}

static void xt_re_parse_concat(xt_re_parser *p, xt_re_concat *out) {
  while (p->s[p->i] && p->s[p->i] != '|' && p->s[p->i] != ')') {
    xt_re_node *node = xt_re_parse_atom(p);
    int quant = XT_RE_Q_NONE;
    if (p->error || !node) {
      p->error = 1;
      return;
    }
    if (p->s[p->i] == '*') {
      quant = XT_RE_Q_STAR;
      p->i++;
    } else if (p->s[p->i] == '+') {
      quant = XT_RE_Q_PLUS;
      p->i++;
    } else if (p->s[p->i] == '?') {
      quant = XT_RE_Q_QUEST;
      p->i++;
    }
    xt_re_push_piece(out, node, quant);
  }
}

static xt_re_alt *xt_re_parse_alt(xt_re_parser *p) {
  xt_re_alt *alt = (xt_re_alt *)calloc(1, sizeof(xt_re_alt));
  if (!alt) {
    p->error = 1;
    return NULL;
  }
  for (;;) {
    xt_re_concat concat;
    memset(&concat, 0, sizeof(concat));
    xt_re_parse_concat(p, &concat);
    if (p->error) {
      free(concat.pieces);
      free(alt->branches);
      free(alt);
      return NULL;
    }
    xt_re_push_branch(alt, concat);
    if (p->s[p->i] == '|') {
      p->i++;
      continue;
    }
    break;
  }
  return alt;
}
