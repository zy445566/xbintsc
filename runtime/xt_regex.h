/*
 * xbintsc runtime — minimal POSIX ERE-compatible regular expressions.
 *
 * Windows does not ship <regex.h>, so on `_WIN32` targets we fall back to this
 * small self-contained matcher. It implements the subset the runtime uses:
 *
 *   literals, '.', '^', '$', '[abc]', '[^abc]', '[a-z]', '*', '+', '?',
 *   alternation '|', groups '(...)', and escapes ('\d', '\w', '\s', ...).
 *
 * Only the boolean "does the pattern match a substring" result is produced;
 * capture groups are not reported (the caller passes nmatch = 0).
 *
 * The implementation is a recursive backtracking matcher over a small AST.
 * Patterns are tiny, so this is fast enough and keeps the runtime free of an
 * external dependency.
 */

#ifndef XT_REGEX_H
#define XT_REGEX_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4

typedef struct {
  int rm_so;
  int rm_eo;
} regmatch_t;

/* Node kinds. */
enum {
  XT_RE_CHAR = 0,
  XT_RE_ANY,
  XT_RE_CLASS,
  XT_RE_BOL,
  XT_RE_EOL,
  XT_RE_GROUP
};

/* Quantifiers. */
enum {
  XT_RE_Q_NONE = 0,
  XT_RE_Q_STAR,
  XT_RE_Q_PLUS,
  XT_RE_Q_QUEST
};

typedef struct xt_re_alt xt_re_alt;

typedef struct {
  int type;
  unsigned char ch;
  unsigned char *classbits; /* 256 bits == 32 bytes, for XT_RE_CLASS */
  xt_re_alt *group;         /* for XT_RE_GROUP */
} xt_re_node;

typedef struct {
  xt_re_node *node;
  int quant;
} xt_re_piece;

typedef struct {
  xt_re_piece *pieces;
  int count;
  int capacity;
} xt_re_concat;

struct xt_re_alt {
  xt_re_concat *branches;
  int count;
  int capacity;
};

typedef struct {
  xt_re_alt *root;
  int icase;
} regex_t;

/* -- small helpers -------------------------------------------------------- */

static void xt_re_set_bit(unsigned char *bits, unsigned char c) {
  bits[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static int xt_re_bit_test(const unsigned char *bits, unsigned char c) {
  return (bits[c >> 3] >> (c & 7)) & 1;
}

static unsigned char xt_re_lower(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 'a');
  return c;
}

static void xt_re_add_range(unsigned char *bits, unsigned char lo, unsigned char hi) {
  unsigned int c;
  if (hi < lo) {
    unsigned char t = lo;
    lo = hi;
    hi = t;
  }
  for (c = lo; c <= hi; c++) xt_re_set_bit(bits, (unsigned char)c);
}

/* Adds a predefined class such as \d or \w. Returns 1 when handled. */
static int xt_re_add_predefined(unsigned char *bits, char e) {
  int c;
  switch (e) {
    case 'd':
      xt_re_add_range(bits, '0', '9');
      return 1;
    case 'D':
      for (c = 0; c < 256; c++)
        if (!(c >= '0' && c <= '9')) xt_re_set_bit(bits, (unsigned char)c);
      return 1;
    case 'w':
      xt_re_add_range(bits, 'a', 'z');
      xt_re_add_range(bits, 'A', 'Z');
      xt_re_add_range(bits, '0', '9');
      xt_re_set_bit(bits, '_');
      return 1;
    case 'W':
      for (c = 0; c < 256; c++) {
        int is_word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
        if (!is_word) xt_re_set_bit(bits, (unsigned char)c);
      }
      return 1;
    case 's':
      xt_re_set_bit(bits, ' ');
      xt_re_set_bit(bits, '\t');
      xt_re_set_bit(bits, '\n');
      xt_re_set_bit(bits, '\r');
      xt_re_set_bit(bits, '\f');
      xt_re_set_bit(bits, '\v');
      return 1;
    case 'S':
      for (c = 0; c < 256; c++) {
        int is_space = c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                       c == '\f' || c == '\v';
        if (!is_space) xt_re_set_bit(bits, (unsigned char)c);
      }
      return 1;
    default:
      return 0;
  }
}

/* -- parser --------------------------------------------------------------- */

typedef struct {
  const char *s;
  size_t i;
  int error;
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
    p->i++;
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

/* -- matching ------------------------------------------------------------- */

typedef struct {
  const char *text;
  size_t len;
  int icase;
} xt_re_input;

typedef int (*xt_re_cont)(void *ctx, size_t pos);

static void xt_re_free_alt(xt_re_alt *alt);

static void xt_re_free_node(xt_re_node *n) {
  if (!n) return;
  if (n->type == XT_RE_GROUP) xt_re_free_alt(n->group);
  free(n->classbits);
  free(n);
}

static void xt_re_free_alt(xt_re_alt *alt) {
  int i;
  if (!alt) return;
  for (i = 0; i < alt->count; i++) {
    int j;
    for (j = 0; j < alt->branches[i].count; j++)
      xt_re_free_node(alt->branches[i].pieces[j].node);
    free(alt->branches[i].pieces);
  }
  free(alt->branches);
  free(alt);
}

static int xt_re_char_eq(const xt_re_input *in, unsigned char a, unsigned char b) {
  if (a == b) return 1;
  if (in->icase && xt_re_lower(a) == xt_re_lower(b)) return 1;
  return 0;
}

static int xt_re_class_match(const unsigned char *bits, unsigned char c, int icase) {
  if (xt_re_bit_test(bits, c)) return 1;
  if (icase) {
    unsigned char l = xt_re_lower(c);
    if (xt_re_bit_test(bits, l)) return 1;
    if (l >= 'a' && l <= 'z' && xt_re_bit_test(bits, (unsigned char)(l - 'a' + 'A'))) return 1;
  }
  return 0;
}

static int xt_re_match_alt(xt_re_alt *alt, const xt_re_input *in, size_t pos,
                           xt_re_cont cont, void *ctx);

static int xt_re_match_atom(xt_re_node *node, const xt_re_input *in, size_t pos,
                            xt_re_cont cont, void *ctx) {
  switch (node->type) {
    case XT_RE_CHAR:
      if (pos < in->len && xt_re_char_eq(in, (unsigned char)in->text[pos], node->ch))
        return cont(ctx, pos + 1);
      return 0;
    case XT_RE_ANY:
      if (pos < in->len && in->text[pos] != '\n') return cont(ctx, pos + 1);
      return 0;
    case XT_RE_CLASS:
      if (pos < in->len && xt_re_class_match(node->classbits, (unsigned char)in->text[pos], in->icase))
        return cont(ctx, pos + 1);
      return 0;
    case XT_RE_BOL:
      return pos == 0 ? cont(ctx, pos) : 0;
    case XT_RE_EOL:
      return pos == in->len ? cont(ctx, pos) : 0;
    case XT_RE_GROUP:
      return xt_re_match_alt(node->group, in, pos, cont, ctx);
    default:
      return 0;
  }
}

typedef struct {
  xt_re_node *atom;
  const xt_re_input *in;
  int min;
  int max; /* -1 == unbounded */
  xt_re_cont cont;
  void *ctx;
} xt_re_repeat;

typedef struct {
  xt_re_repeat *rep;
  size_t pos;
  int count;
} xt_re_repeat_step;

static int xt_re_repeat_try(xt_re_repeat *rep, size_t pos, int count);

static int xt_re_repeat_next(void *v, size_t newpos) {
  xt_re_repeat_step *s = (xt_re_repeat_step *)v;
  if (newpos == s->pos) return 0; /* no progress -> stop to avoid loops */
  return xt_re_repeat_try(s->rep, newpos, s->count + 1);
}

static int xt_re_repeat_try(xt_re_repeat *rep, size_t pos, int count) {
  if (rep->max < 0 || count < rep->max) {
    xt_re_repeat_step step;
    step.rep = rep;
    step.pos = pos;
    step.count = count;
    if (xt_re_match_atom(rep->atom, rep->in, pos, xt_re_repeat_next, &step)) return 1;
  }
  if (count >= rep->min) return rep->cont(rep->ctx, pos);
  return 0;
}

static int xt_re_match_piece(xt_re_piece *piece, const xt_re_input *in, size_t pos,
                             xt_re_cont cont, void *ctx) {
  xt_re_repeat rep;
  switch (piece->quant) {
    case XT_RE_Q_STAR:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 0;
      rep.max = -1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0);
    case XT_RE_Q_PLUS:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 1;
      rep.max = -1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0);
    case XT_RE_Q_QUEST:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 0;
      rep.max = 1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0);
    default:
      return xt_re_match_atom(piece->node, in, pos, cont, ctx);
  }
}

typedef struct {
  xt_re_concat *concat;
  const xt_re_input *in;
  xt_re_cont cont;
  void *ctx;
} xt_re_concat_state;

typedef struct {
  xt_re_concat_state *state;
  int index;
} xt_re_concat_step;

static int xt_re_match_concat(xt_re_concat_state *state, int index, size_t pos);

/* The continuation for a concat piece resumes matching at the next piece. */
static int xt_re_concat_next(void *v, size_t pos) {
  xt_re_concat_step *s = (xt_re_concat_step *)v;
  return xt_re_match_concat(s->state, s->index + 1, pos);
}

static int xt_re_match_concat(xt_re_concat_state *state, int index, size_t pos) {
  xt_re_concat_step step;
  if (index >= state->concat->count) return state->cont(state->ctx, pos);
  step.state = state;
  step.index = index;
  return xt_re_match_piece(&state->concat->pieces[index], state->in, pos,
                           xt_re_concat_next, &step);
}

static int xt_re_match_alt(xt_re_alt *alt, const xt_re_input *in, size_t pos,
                           xt_re_cont cont, void *ctx) {
  int i;
  for (i = 0; i < alt->count; i++) {
    xt_re_concat_state state;
    state.concat = &alt->branches[i];
    state.in = in;
    state.cont = cont;
    state.ctx = ctx;
    if (xt_re_match_concat(&state, 0, pos)) return 1;
  }
  return 0;
}

static int xt_re_accept(void *ctx, size_t pos) {
  (void)ctx;
  (void)pos;
  return 1;
}

/* -- public API ----------------------------------------------------------- */

static int regcomp(regex_t *preg, const char *pattern, int cflags) {
  xt_re_parser p;
  xt_re_alt *root;
  if (!preg || !pattern) return 1;
  memset(preg, 0, sizeof(*preg));
  p.s = pattern;
  p.i = 0;
  p.error = 0;
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
  (void)nmatch;
  (void)pmatch;
  (void)eflags;
  if (!preg || !preg->root || !string) return 1;
  in.text = string;
  in.len = strlen(string);
  in.icase = preg->icase;
  for (start = 0; start <= in.len; start++) {
    if (xt_re_match_alt(preg->root, &in, start, xt_re_accept, NULL)) return 0;
  }
  return 1;
}

static void regfree(regex_t *preg) {
  if (!preg) return;
  xt_re_free_alt(preg->root);
  preg->root = NULL;
}

#endif /* XT_REGEX_H */
