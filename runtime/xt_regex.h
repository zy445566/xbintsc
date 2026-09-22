/*
 * xbintsc runtime — minimal POSIX ERE-compatible regular expressions.
 *
 * Windows does not ship <regex.h>, so on `_WIN32` targets we fall back to this
 * small self-contained matcher. It implements the subset the runtime uses:
 *
 *   literals, '.', '^', '$', '[abc]', '[^abc]', '[a-z]', '*', '+', '?',
 *   alternation '|', groups '(...)', and escapes ('\d', '\w', '\s', ...).
 *
 * Capturing groups are reported through `regmatch_t` the same way POSIX does:
 * `pmatch[0]` is the whole match and `pmatch[k]` the k-th group in pattern
 * order, with unmatched groups left at -1. The implementation is a recursive
 * backtracking matcher over a small AST; captures are carried on an immutable
 * chain of stack frames so abandoned search paths are undone automatically.
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
  int groupIndex;           /* 1-based capture number, for XT_RE_GROUP */
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

/* -- matching ------------------------------------------------------------- */

typedef struct {
  const char *text;
  size_t len;
  int icase;
} xt_re_input;

/* One capture on the immutable chain of the current search path. `parent`
 * points to the captures that were live when the group was entered, so an
 * abandoned backtrack simply drops the frame instead of restoring it. */
typedef struct xt_re_cap {
  const struct xt_re_cap *parent;
  int index; /* 1-based capture group number */
  int start;
  int end;
} xt_re_cap;

typedef int (*xt_re_cont)(void *ctx, size_t pos, const xt_re_cap *caps);

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
                           xt_re_cont cont, void *ctx, const xt_re_cap *caps);

/* Resumes the outer continuation once a group's body has matched, recording
 * where the group ended on the frame it pushed. */
typedef struct {
  xt_re_cap *frame;
  xt_re_cont cont;
  void *ctx;
} xt_re_group_state;

static int xt_re_group_next(void *v, size_t pos, const xt_re_cap *caps) {
  xt_re_group_state *s = (xt_re_group_state *)v;
  /* `caps` already heads the chain that includes this group's own frame (and
   * any groups nested inside it), so forward it as-is. */
  s->frame->end = (int)pos;
  return s->cont(s->ctx, pos, caps);
}

static int xt_re_match_atom(xt_re_node *node, const xt_re_input *in, size_t pos,
                            xt_re_cont cont, void *ctx, const xt_re_cap *caps) {
  switch (node->type) {
    case XT_RE_CHAR:
      if (pos < in->len && xt_re_char_eq(in, (unsigned char)in->text[pos], node->ch))
        return cont(ctx, pos + 1, caps);
      return 0;
    case XT_RE_ANY:
      if (pos < in->len && in->text[pos] != '\n') return cont(ctx, pos + 1, caps);
      return 0;
    case XT_RE_CLASS:
      if (pos < in->len && xt_re_class_match(node->classbits, (unsigned char)in->text[pos], in->icase))
        return cont(ctx, pos + 1, caps);
      return 0;
    case XT_RE_BOL:
      return pos == 0 ? cont(ctx, pos, caps) : 0;
    case XT_RE_EOL:
      return pos == in->len ? cont(ctx, pos, caps) : 0;
    case XT_RE_GROUP: {
      xt_re_cap frame;
      xt_re_group_state state;
      frame.parent = caps;
      frame.index = node->groupIndex;
      frame.start = (int)pos;
      frame.end = -1;
      state.frame = &frame;
      state.cont = cont;
      state.ctx = ctx;
      return xt_re_match_alt(node->group, in, pos, xt_re_group_next, &state, &frame);
    }
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

static int xt_re_repeat_try(xt_re_repeat *rep, size_t pos, int count, const xt_re_cap *caps);

static int xt_re_repeat_next(void *v, size_t newpos, const xt_re_cap *caps) {
  xt_re_repeat_step *s = (xt_re_repeat_step *)v;
  if (newpos == s->pos) return 0; /* no progress -> stop to avoid loops */
  return xt_re_repeat_try(s->rep, newpos, s->count + 1, caps);
}

static int xt_re_repeat_try(xt_re_repeat *rep, size_t pos, int count, const xt_re_cap *caps) {
  if (rep->max < 0 || count < rep->max) {
    xt_re_repeat_step step;
    step.rep = rep;
    step.pos = pos;
    step.count = count;
    if (xt_re_match_atom(rep->atom, rep->in, pos, xt_re_repeat_next, &step, caps)) return 1;
  }
  if (count >= rep->min) return rep->cont(rep->ctx, pos, caps);
  return 0;
}

static int xt_re_match_piece(xt_re_piece *piece, const xt_re_input *in, size_t pos,
                             xt_re_cont cont, void *ctx, const xt_re_cap *caps) {
  xt_re_repeat rep;
  switch (piece->quant) {
    case XT_RE_Q_STAR:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 0;
      rep.max = -1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0, caps);
    case XT_RE_Q_PLUS:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 1;
      rep.max = -1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0, caps);
    case XT_RE_Q_QUEST:
      rep.atom = piece->node;
      rep.in = in;
      rep.min = 0;
      rep.max = 1;
      rep.cont = cont;
      rep.ctx = ctx;
      return xt_re_repeat_try(&rep, pos, 0, caps);
    default:
      return xt_re_match_atom(piece->node, in, pos, cont, ctx, caps);
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

static int xt_re_match_concat(xt_re_concat_state *state, int index, size_t pos, const xt_re_cap *caps);

/* The continuation for a concat piece resumes matching at the next piece. */
static int xt_re_concat_next(void *v, size_t pos, const xt_re_cap *caps) {
  xt_re_concat_step *s = (xt_re_concat_step *)v;
  return xt_re_match_concat(s->state, s->index + 1, pos, caps);
}

static int xt_re_match_concat(xt_re_concat_state *state, int index, size_t pos, const xt_re_cap *caps) {
  xt_re_concat_step step;
  if (index >= state->concat->count) return state->cont(state->ctx, pos, caps);
  step.state = state;
  step.index = index;
  return xt_re_match_piece(&state->concat->pieces[index], state->in, pos,
                           xt_re_concat_next, &step, caps);
}

static int xt_re_match_alt(xt_re_alt *alt, const xt_re_input *in, size_t pos,
                           xt_re_cont cont, void *ctx, const xt_re_cap *caps) {
  int i;
  for (i = 0; i < alt->count; i++) {
    xt_re_concat_state state;
    state.concat = &alt->branches[i];
    state.in = in;
    state.cont = cont;
    state.ctx = ctx;
    if (xt_re_match_concat(&state, 0, pos, caps)) return 1;
  }
  return 0;
}

typedef struct {
  size_t end;
  regmatch_t *pmatch;
  size_t nmatch;
} xt_re_match_result;

/* Final continuation: records the whole-match end offset and copies every
 * capture on the successful path into `pmatch`. Walking from the head means
 * the most recent frame wins, which is what a quantified group such as
 * `(a)+` should report (its last iteration). */
static int xt_re_record_match(void *ctx, size_t pos, const xt_re_cap *caps) {
  xt_re_match_result *result = (xt_re_match_result *)ctx;
  const xt_re_cap *cap;
  result->end = pos;
  for (cap = caps; cap; cap = cap->parent) {
    if (cap->index >= 1 && (size_t)cap->index < result->nmatch) {
      regmatch_t *slot = &result->pmatch[cap->index];
      if (slot->rm_so < 0) {
        slot->rm_so = cap->start;
        slot->rm_eo = cap->end;
      }
    }
  }
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

#endif /* XT_REGEX_H */
