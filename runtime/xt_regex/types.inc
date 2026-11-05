#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NOSUB 4
#define REG_MULTILINE 8

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
  XT_RE_Q_QUEST,
  XT_RE_Q_RANGE
};

/** Upper bound for a counted quantifier (`a{0,20000}`): the matcher recurses
 * once per repetition, so an unbounded authored bound could overflow the C
 * stack. A brace group whose count exceeds this is treated as literal text,
 * exactly like any other malformed `{...}`. */
#define XT_RE_MAX_REPEAT 4096

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
  int lazy;
  /** Repetition bounds for the quantifier (`max == -1` means unbounded). */
  int min;
  int max;
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
  int multiline;
} regex_t;
