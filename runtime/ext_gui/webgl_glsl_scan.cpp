/*
 * xbintsc GUI engine — WebGL GLSL scanner.
 *
 * Splits a shader into tokens (keeping byte offsets so the rewriter can splice
 * the original text without disturbing comments or preprocessor lines) and
 * records every top-level declaration WebGL has to translate.
 */

#include "webgl_glsl.h"

#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace xtgui {
namespace {

struct Token {
  enum Kind { Ident, Number, Punct, Directive } kind = Punct;
  std::string text;
  size_t begin = 0;
  size_t end = 0;
};

bool is_ident_start(char c) {
  return std::isalpha((unsigned char)c) != 0 || c == '_';
}

bool is_ident_char(char c) {
  return std::isalnum((unsigned char)c) != 0 || c == '_';
}

/** Tokenise GLSL. Comments are skipped (their bytes stay in the source); `#`
 * directives are one Directive token running to the end of the line. */
std::vector<Token> tokenize(const std::string &source) {
  std::vector<Token> tokens;
  size_t i = 0;
  const size_t size = source.size();
  while (i < size) {
    const char c = source[i];
    if (std::isspace((unsigned char)c) != 0) {
      i++;
      continue;
    }
    if (c == '/' && i + 1 < size && source[i + 1] == '/') {
      while (i < size && source[i] != '\n') i++;
      continue;
    }
    if (c == '/' && i + 1 < size && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < size && !(source[i] == '*' && source[i + 1] == '/')) i++;
      if (i + 1 < size) i += 2;
      continue;
    }
    Token token;
    token.begin = i;
    if (c == '#') {
      size_t line_end = i;
      while (line_end < size && source[line_end] != '\n') line_end++;
      token.kind = Token::Directive;
      token.text = source.substr(i, line_end - i);
      token.end = line_end;
      tokens.push_back(token);
      i = line_end;
      continue;
    }
    if (is_ident_start(c)) {
      size_t end = i + 1;
      while (end < size && is_ident_char(source[end])) end++;
      token.kind = Token::Ident;
      token.text = source.substr(i, end - i);
      token.end = end;
      tokens.push_back(token);
      i = end;
      continue;
    }
    if (std::isdigit((unsigned char)c) != 0 ||
        (c == '.' && i + 1 < size && std::isdigit((unsigned char)source[i + 1]) != 0)) {
      size_t end = i + 1;
      while (end < size &&
             (std::isalnum((unsigned char)source[end]) != 0 || source[end] == '.' ||
              source[end] == '_')) {
        end++;
      }
      token.kind = Token::Number;
      token.text = source.substr(i, end - i);
      token.end = end;
      tokens.push_back(token);
      i = end;
      continue;
    }
    token.kind = Token::Punct;
    token.text = std::string(1, c);
    token.end = i + 1;
    tokens.push_back(token);
    i++;
  }
  return tokens;
}

bool is_qualifier(const std::string &text) {
  return text == "highp" || text == "mediump" || text == "lowp" || text == "flat" ||
         text == "smooth" || text == "centroid" || text == "invariant" || text == "precise" ||
         text == "noperspective" || text == "sample" || text == "const";
}

bool is_sampler_type(const std::string &type) {
  return type.rfind("sampler", 0) == 0 || type == "atomic_uint";
}

/** The scalar/vector/matrix types the std140 block reflection understands. */
bool is_basic_type(const std::string &type) {
  if (is_sampler_type(type)) return true;
  return type == "float" || type == "int" || type == "uint" || type == "bool" ||
         type == "vec2" || type == "vec3" || type == "vec4" || type == "ivec2" ||
         type == "ivec3" || type == "ivec4" || type == "uvec2" || type == "uvec3" ||
         type == "uvec4" || type == "bvec2" || type == "bvec3" || type == "bvec4" ||
         type == "mat2" || type == "mat3" || type == "mat4";
}

/** The `location = N` inside a `layout(...)` group starting at token `at`. */
int parse_layout_location(const std::vector<Token> &tokens, size_t at, size_t *after) {
  int location = -1;
  if (at >= tokens.size() || tokens[at].text != "layout") return -1;
  size_t i = at + 1;
  if (i >= tokens.size() || tokens[i].text != "(") return -1;
  int depth = 1;
  i++;
  while (i < tokens.size() && depth > 0) {
    if (tokens[i].text == "(") depth++;
    if (tokens[i].text == ")") depth--;
    if (depth > 0 && tokens[i].text == "location" && i + 2 < tokens.size() &&
        tokens[i + 1].text == "=" && tokens[i + 2].kind == Token::Number) {
      location = (int)std::strtol(tokens[i + 2].text.c_str(), nullptr, 0);
    }
    i++;
  }
  *after = i;
  return location;
}

/** The byte range of one declarator (between commas at bracket depth 0). */
struct Declarator {
  size_t first = 0;
  size_t last = 0;  // inclusive token index
};

std::vector<Declarator> split_declarators(const std::vector<Token> &tokens, size_t first,
                                          size_t semicolon) {
  std::vector<Declarator> parts;
  Declarator current;
  current.first = first;
  int brackets = 0;
  int parens = 0;
  for (size_t i = first; i < semicolon; i++) {
    const std::string &text = tokens[i].text;
    if (text == "[") brackets++;
    if (text == "]") brackets--;
    if (text == "(") parens++;
    if (text == ")") parens--;
    if (text == "," && brackets == 0 && parens == 0) {
      current.last = i - 1;
      parts.push_back(current);
      current = Declarator();
      current.first = i + 1;
    }
  }
  current.last = semicolon - 1;
  parts.push_back(current);
  return parts;
}

/** Parse one declarator: `[precision] type name[array] [= init]`. */
GlslUniformDecl parse_declarator(const std::vector<Token> &tokens, const Declarator &part,
                                 const std::string &type, bool first_part) {
  GlslUniformDecl decl;
  decl.type = type;
  size_t i = part.first;
  if (first_part) {
    while (i <= part.last && tokens[i].kind == Token::Ident && is_qualifier(tokens[i].text)) {
      decl.precision = tokens[i].text;
      i++;
    }
    /* The type is the first identifier after the qualifiers. */
    if (i > part.last || tokens[i].kind != Token::Ident) return decl;
    decl.type = tokens[i].text;
    i++;
  }
  while (i <= part.last && tokens[i].kind != Token::Ident) i++;
  if (i > part.last) return decl;
  decl.name = tokens[i].text;
  i++;
  if (i <= part.last && tokens[i].text == "[") {
    int depth = 0;
    for (; i <= part.last; i++) {
      decl.array += tokens[i].text;
      if (tokens[i].text == "[") depth++;
      if (tokens[i].text == "]" && --depth == 0) break;
    }
  }
  decl.sampler = is_sampler_type(decl.type);
  return decl;
}

/* -- preprocessor conditions --------------------------------------------- */

/** Every macro a `#define` in the source introduces. A name is treated as
 * defined even when its `#define` sits in a conditional branch; that only makes
 * the condition evaluator more conservative (it may keep a declaration that
 * would be inactive, never drop one that is active). */
std::set<std::string> collect_defined_names(const std::vector<Token> &tokens) {
  std::set<std::string> names;
  for (const Token &token : tokens) {
    if (token.kind != Token::Directive) continue;
    size_t at = token.text.find_first_not_of(" \t");
    if (at == std::string::npos || token.text.compare(at, 7, "#define") != 0) continue;
    at += 7;
    while (at < token.text.size() && (token.text[at] == ' ' || token.text[at] == '\t')) at++;
    size_t end = at;
    while (end < token.text.size() &&
           (std::isalnum((unsigned char)token.text[end]) != 0 || token.text[end] == '_')) {
      end++;
    }
    if (end > at && std::isalpha((unsigned char)token.text[at]) != 0) {
      names.insert(token.text.substr(at, end - at));
    }
  }
  return names;
}

/** Turns `defined(X)` / `defined X` into 1/0 and leaves the logical operators,
 * parentheses and literals. An identifier or a numeric macro makes the
 * expression "unknown" (`?`), which callers treat as active. */
std::string normalize_condition(const std::string &expression,
                                const std::set<std::string> &defined) {
  std::string out;
  for (size_t i = 0; i < expression.size();) {
    const char c = expression[i];
    if (std::isalpha((unsigned char)c) != 0 || c == '_') {
      size_t end = i;
      while (end < expression.size() &&
             (std::isalnum((unsigned char)expression[end]) != 0 || expression[end] == '_')) {
        end++;
      }
      const std::string word = expression.substr(i, end - i);
      if (word == "defined") {
        size_t at = end;
        while (at < expression.size() && std::isspace((unsigned char)expression[at]) != 0) at++;
        const bool paren = at < expression.size() && expression[at] == '(';
        if (paren) {
          at++;
          while (at < expression.size() && std::isspace((unsigned char)expression[at]) != 0) at++;
        }
        const size_t begin = at;
        while (at < expression.size() &&
               (std::isalnum((unsigned char)expression[at]) != 0 || expression[at] == '_')) {
          at++;
        }
        const std::string name = expression.substr(begin, at - begin);
        if (paren) {
          while (at < expression.size() && std::isspace((unsigned char)expression[at]) != 0) at++;
          if (at < expression.size() && expression[at] == ')') at++;
        }
        out += defined.count(name) != 0 ? '1' : '0';
        i = at;
        continue;
      }
      out += '?';
      i = end;
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

struct ConditionValue {
  bool value = true;
  bool known = true;
};

class ConditionParser {
 public:
  explicit ConditionParser(const std::string &text) : text_(text) {}

  ConditionValue parse() {
    const bool value = parse_or();
    return {value, known_};
  }

 private:
  void skip() {
    while (at_ < text_.size() && std::isspace((unsigned char)text_[at_]) != 0) at_++;
  }
  bool parse_or() {
    bool value = parse_and();
    for (;;) {
      skip();
      if (at_ + 1 >= text_.size() || text_[at_] != '|' || text_[at_ + 1] != '|') break;
      at_ += 2;
      value = parse_and() || value;
    }
    return value;
  }
  bool parse_and() {
    bool value = parse_unary();
    for (;;) {
      skip();
      if (at_ + 1 >= text_.size() || text_[at_] != '&' || text_[at_ + 1] != '&') break;
      at_ += 2;
      value = parse_unary() && value;
    }
    return value;
  }
  bool parse_unary() {
    skip();
    if (at_ < text_.size() && text_[at_] == '!') {
      at_++;
      return !parse_unary();
    }
    return parse_primary();
  }
  bool parse_primary() {
    skip();
    if (at_ >= text_.size()) {
      known_ = false;
      return false;
    }
    if (text_[at_] == '(') {
      at_++;
      const bool value = parse_or();
      skip();
      if (at_ < text_.size() && text_[at_] == ')') at_++;
      return value;
    }
    if (text_[at_] == '1' || text_[at_] == '0') {
      return text_[at_++] == '1';
    }
    known_ = false;
    at_++;
    return false;
  }

  const std::string &text_;
  size_t at_ = 0;
  bool known_ = true;
};

ConditionValue evaluate_condition(const std::string &directive,
                                  const std::set<std::string> &defined) {
  std::string text = directive;
  size_t at = text.find_first_not_of(" \t#");
  if (at == std::string::npos) return {true, false};
  const bool is_ifdef = text.compare(at, 5, "ifdef") == 0;
  const bool is_ifndef = text.compare(at, 6, "ifndef") == 0;
  if (is_ifdef || is_ifndef) {
    at += is_ifdef ? 5 : 6;
    while (at < text.size() && std::isspace((unsigned char)text[at]) != 0) at++;
    size_t end = at;
    while (end < text.size() &&
           (std::isalnum((unsigned char)text[end]) != 0 || text[end] == '_')) {
      end++;
    }
    if (end == at) return {true, false};
    const std::string name = text.substr(at, end - at);
    const bool present = defined.count(name) != 0;
    return {is_ifdef ? present : !present, true};
  }
  if (text.compare(at, 2, "if") != 0) return {true, false};
  const std::string normalized = normalize_condition(text.substr(at + 2), defined);
  ConditionParser parser(normalized);
  return parser.parse();
}

}  // namespace

void scan_glsl(const std::string &source, bool fragment, GlslShaderInfo *info) {
  *info = GlslShaderInfo();
  const std::vector<Token> tokens = tokenize(source);
  const std::set<std::string> defined_names = collect_defined_names(tokens);
  std::vector<bool> condition_stack;
  int inactive = 0;
  int braces = 0;
  int parens = 0;
  size_t first_uniform_any = std::string::npos;
  size_t pending_layout = std::string::npos;
  int pending_location = -1;

  for (size_t i = 0; i < tokens.size(); i++) {
    const Token &token = tokens[i];
    if (token.kind == Token::Directive) {
      const std::string &directive = token.text;
      size_t at = directive.find_first_not_of(" \t");
      if (at == std::string::npos) continue;
      if (directive.compare(at, 6, "#endif") == 0) {
        if (!condition_stack.empty()) {
          if (!condition_stack.back()) inactive--;
          condition_stack.pop_back();
        }
      } else if (directive.compare(at, 5, "#elif") == 0) {
        if (!condition_stack.empty()) {
          const ConditionValue cond = evaluate_condition(directive, defined_names);
          const bool active = !cond.known || cond.value;
          if (!condition_stack.back()) inactive--;
          condition_stack.back() = active;
          if (!active) inactive++;
        }
      } else if (directive.compare(at, 5, "#else") == 0) {
        if (!condition_stack.empty()) {
          const bool was_active = condition_stack.back();
          if (was_active) inactive++;
          else inactive--;
          condition_stack.back() = !was_active;
        }
      } else if (directive.compare(at, 3, "#if") == 0) {
        const ConditionValue cond = evaluate_condition(directive, defined_names);
        const bool active = !cond.known || cond.value;
        condition_stack.push_back(active);
        if (!active) inactive++;
      }
      continue;
    }
    if (token.kind == Token::Punct) {
      if (token.text == "{") braces++;
      if (token.text == "}") braces--;
      if (token.text == "(") parens++;
      if (token.text == ")") parens--;
      continue;
    }
    if (token.kind == Token::Ident && token.text == "gl_FragColor") {
      info->legacy_fragment_output = true;
      continue;
    }
    if (token.kind == Token::Ident && token.text == "gl_FragData") {
      info->legacy_fragment_output = true;
      continue;
    }
    /* Declarations inside a definitively false preprocessor branch never reach
     * the compiler; rewriting them would move their types/macros (and array
     * sizes such as `MORPHTARGETS_COUNT`) outside that branch. */
    if (inactive > 0) continue;
    /* Function parameters may use `in`/`out`/`const in` qualifiers; those are
     * not declarations to rewrite (a layout qualifier there is a compile
     * error). */
    if (braces != 0 || parens != 0) continue;
    if (token.kind != Token::Ident) continue;
    if (token.text == "layout") {
      pending_layout = i;
      size_t after = i;
      pending_location = parse_layout_location(tokens, i, &after);
      i = after - 1;
      continue;
    }

    GlslDeclSite::Kind kind;
    bool is_decl = true;
    if (token.text == "uniform") {
      kind = GlslDeclSite::Uniform;
    } else if (token.text == "attribute") {
      kind = GlslDeclSite::Attribute;
    } else if (token.text == "varying") {
      kind = GlslDeclSite::Varying;
    } else if (token.text == "in") {
      kind = fragment ? GlslDeclSite::Varying : GlslDeclSite::Attribute;
    } else if (token.text == "out") {
      if (!fragment) {
        kind = GlslDeclSite::Varying;
      } else {
        kind = GlslDeclSite::FragmentOut;
        info->has_fragment_output = true;
      }
    } else {
      is_decl = false;
    }
    if (!is_decl) {
      pending_layout = std::string::npos;
      pending_location = -1;
      continue;
    }

    /* Find the terminating semicolon at bracket/paren depth 0. */
    size_t semicolon = i + 1;
    int brackets = 0;
    int parens = 0;
    for (; semicolon < tokens.size(); semicolon++) {
      const std::string &text = tokens[semicolon].text;
      if (text == "[") brackets++;
      if (text == "]") brackets--;
      if (text == "(") parens++;
      if (text == ")") parens--;
      if (text == ";" && brackets == 0 && parens == 0) break;
      if (text == "{" && brackets == 0 && parens == 0 && kind == GlslDeclSite::Uniform) {
        info->unsupported = "uniform blocks are not supported";
      }
    }
    if (semicolon >= tokens.size()) break;

    const std::vector<Declarator> parts = split_declarators(tokens, i + 1, semicolon);
    /* The type is the first non-qualifier identifier of the first declarator. */
    std::string type;
    {
      size_t at = parts[0].first;
      while (at <= parts[0].last && tokens[at].kind == Token::Ident &&
             is_qualifier(tokens[at].text)) {
        at++;
      }
      if (at <= parts[0].last && tokens[at].kind == Token::Ident) type = tokens[at].text;
    }
    for (size_t p = 0; p < parts.size(); p++) {
      GlslUniformDecl decl = parse_declarator(tokens, parts[p], type, p == 0);
      if (decl.name.empty()) continue;
      if (kind == GlslDeclSite::Uniform && !is_basic_type(decl.type) &&
          info->unsupported.empty()) {
        info->unsupported = "uniform type '" + decl.type + "' is not supported";
      }
      GlslDeclSite site;
      site.kind = decl.sampler && kind == GlslDeclSite::Uniform ? GlslDeclSite::Sampler : kind;
      site.begin = (p == 0 && pending_layout != std::string::npos) ? tokens[pending_layout].begin
                                                                  : tokens[i].begin;
      site.end = tokens[semicolon].end;
      site.decl = decl;
      site.existing_location = p == 0 ? pending_location : -1;
      site.conditional = !condition_stack.empty();
      info->sites.push_back(site);
      switch (site.kind) {
        case GlslDeclSite::Uniform:
          info->uniforms.push_back(decl);
          if (first_uniform_any == std::string::npos) first_uniform_any = site.begin;
          if (!site.conditional && info->first_uniform_offset == std::string::npos) {
            info->first_uniform_offset = site.begin;
          }
          break;
        case GlslDeclSite::Sampler:
          info->samplers.push_back(decl);
          break;
        case GlslDeclSite::Attribute:
          info->attributes.push_back(decl);
          break;
        case GlslDeclSite::Varying:
          info->varyings.push_back(decl);
          break;
        case GlslDeclSite::FragmentOut:
          break;
      }
    }
    i = semicolon;
    pending_layout = std::string::npos;
    pending_location = -1;
  }
  /* Every non-sampler uniform was inside a preprocessor branch; fall back to
   * the first one so the block still has a home. */
  if (info->first_uniform_offset == std::string::npos) {
    info->first_uniform_offset = first_uniform_any;
  }
}

}  // namespace xtgui
