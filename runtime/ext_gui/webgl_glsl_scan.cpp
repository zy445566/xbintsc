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

}  // namespace

void scan_glsl(const std::string &source, bool fragment, GlslShaderInfo *info) {
  *info = GlslShaderInfo();
  const std::vector<Token> tokens = tokenize(source);
  int braces = 0;
  size_t pending_layout = std::string::npos;
  int pending_location = -1;

  for (size_t i = 0; i < tokens.size(); i++) {
    const Token &token = tokens[i];
    if (token.kind == Token::Punct) {
      if (token.text == "{") braces++;
      if (token.text == "}") braces--;
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
    if (braces != 0) continue;
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
      info->sites.push_back(site);
      switch (site.kind) {
        case GlslDeclSite::Uniform:
          info->uniforms.push_back(decl);
          if (info->first_uniform_offset == std::string::npos) {
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
}

}  // namespace xtgui
