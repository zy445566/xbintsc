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
