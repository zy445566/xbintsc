/*
 * xbintsc GUI engine — HTML tokenizer + tree builder.
 *
 * The tokenizer is a single pass that also swallows the contents of raw-text
 * elements (<style>, <script>, ...). The tree builder keeps a stack of open
 * elements and applies a small set of HTML "implied end tag" rules, which is
 * all a UI document needs.
 */

#include "dom.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace xtgui {

/* -- Node helpers --------------------------------------------------------- */

const std::string *Node::attr(const std::string &name) const {
  for (const Attr &entry : attrs) {
    if (entry.name == name) return &entry.value;
  }
  return nullptr;
}

bool Node::isTag(const char *name) const {
  return type == NodeType::Element && tag == name;
}

Node *Node::append(std::unique_ptr<Node> child) {
  child->parent = this;
  Node *raw = child.get();
  children.push_back(std::move(child));
  return raw;
}

Node *Node::addElement(const std::string &tagName) {
  auto node = std::make_unique<Node>(NodeType::Element);
  node->tag = tagName;
  return append(std::move(node));
}

Node *Node::addText(const std::string &data) {
  if (data.empty()) return nullptr;
  auto node = std::make_unique<Node>(NodeType::Text);
  node->text = data;
  return append(std::move(node));
}

Node *Node::firstElementChild() const {
  for (const std::unique_ptr<Node> &child : children) {
    if (child->type == NodeType::Element) return child.get();
  }
  return nullptr;
}

int Node::elementIndex() const {
  if (parent == nullptr) return 0;
  int index = 0;
  for (const std::unique_ptr<Node> &sibling : parent->children) {
    if (sibling.get() == this) break;
    if (sibling->type == NodeType::Element) index++;
  }
  return index;
}

int Node::elementSiblingCount() const {
  if (parent == nullptr) return 1;
  int count = 0;
  for (const std::unique_ptr<Node> &sibling : parent->children) {
    if (sibling->type == NodeType::Element) count++;
  }
  return count;
}

/* -- helpers -------------------------------------------------------------- */

static bool isAsciiAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static std::string toLower(const std::string &text) {
  std::string out = text;
  for (char &c : out) c = (char)std::tolower((unsigned char)c);
  return out;
}

static std::string trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && isWhitespace(text[begin])) begin++;
  while (end > begin && isWhitespace(text[end - 1])) end--;
  return text.substr(begin, end - begin);
}

/** Decode the handful of entities that show up in UI text. */
static std::string decodeEntities(const std::string &text) {
  if (text.find('&') == std::string::npos) return text;
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }
    size_t semi = text.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
      out.push_back('&');
      continue;
    }
    std::string name = text.substr(i + 1, semi - i - 1);
    if (name == "amp") out.push_back('&');
    else if (name == "lt") out.push_back('<');
    else if (name == "gt") out.push_back('>');
    else if (name == "quot") out.push_back('"');
    else if (name == "apos") out.push_back('\'');
    else if (name == "nbsp") out.push_back(' ');
    else if (!name.empty() && name[0] == '#') {
      long code = 0;
      if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X')) {
        for (size_t k = 2; k < name.size(); k++) {
          char digit = (char)std::tolower((unsigned char)name[k]);
          int value = (digit >= '0' && digit <= '9') ? digit - '0'
                      : (digit >= 'a' && digit <= 'f') ? digit - 'a' + 10
                                                       : -1;
          if (value < 0) break;
          code = code * 16 + value;
        }
      } else {
        code = strtol(name.c_str() + 1, nullptr, 10);
      }
      if (code > 0 && code < 128) out.push_back((char)code);
      else out.push_back('?');
    } else {
      out.append(text, i, semi - i + 1);
      i = semi;
      continue;
    }
    i = semi;
  }
  return out;
}

static const std::unordered_map<std::string, bool> &voidElements() {
  static const std::unordered_map<std::string, bool> set = {
      {"area", true},   {"base", true},   {"br", true},    {"col", true},
      {"embed", true},  {"hr", true},     {"img", true},   {"input", true},
      {"link", true},   {"meta", true},   {"param", true}, {"source", true},
      {"track", true},  {"wbr", true},
  };
  return set;
}

/** Elements whose content is treated as raw text. `script` is discarded. */
static bool isRawText(const std::string &tag, bool *keepText) {
  if (tag == "style" || tag == "title" || tag == "textarea") {
    *keepText = true;
    return true;
  }
  if (tag == "script" || tag == "xmp" || tag == "iframe" || tag == "noembed" ||
      tag == "noframes" || tag == "plaintext") {
    *keepText = false;
    return true;
  }
  return false;
}

/* -- tokenizer ------------------------------------------------------------ */

enum class TokenKind { Start, End, Text, EndOfFile };

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  std::string name;
  std::vector<Attr> attrs;
  bool selfClosing = false;
};

class Tokenizer {
 public:
  explicit Tokenizer(const std::string &html) : html_(html) {}

  std::vector<Token> run() {
    std::vector<Token> tokens;
    while (position_ < html_.size()) {
      if (html_[position_] == '<' && position_ + 1 < html_.size()) {
        char next = html_[position_ + 1];
        if (next == '!') {
          skipBogusComment();
          continue;
        }
        if (next == '/') {
          Token token = readEndTag();
          if (!token.name.empty()) tokens.push_back(std::move(token));
          continue;
        }
        if (isAsciiAlpha(next)) {
          Token token = readStartTag();
          tokens.push_back(token);
          bool keepText = false;
          if (isRawText(token.name, &keepText) && !token.selfClosing) {
            readRawText(token.name, keepText, tokens);
          }
          continue;
        }
      }
      std::string text = readText();
      if (!text.empty()) {
        Token token;
        token.kind = TokenKind::Text;
        token.name = decodeEntities(text);
        tokens.push_back(std::move(token));
      }
    }
    return tokens;
  }

 private:
  const std::string &html_;
  size_t position_ = 0;

  void skipBogusComment() {
    if (html_.compare(position_, 4, "<!--") == 0) {
      size_t end = html_.find("-->", position_ + 4);
      position_ = (end == std::string::npos) ? html_.size() : end + 3;
      return;
    }
    size_t end = html_.find('>', position_);
    position_ = (end == std::string::npos) ? html_.size() : end + 1;
  }

  std::string readText() {
    size_t start = position_;
    while (position_ < html_.size() && html_[position_] != '<') position_++;
    return html_.substr(start, position_ - start);
  }

  std::string readName() {
    size_t start = position_;
    while (position_ < html_.size()) {
      char c = html_[position_];
      if (isWhitespace(c) || c == '/' || c == '>') break;
      position_++;
    }
    return toLower(html_.substr(start, position_ - start));
  }

  void skipWhitespace() {
    while (position_ < html_.size() && isWhitespace(html_[position_])) position_++;
  }

  Token readStartTag() {
    Token token;
    token.kind = TokenKind::Start;
    position_++;  // consume '<'
    token.name = readName();
    while (position_ < html_.size()) {
      skipWhitespace();
      if (position_ >= html_.size()) break;
      char c = html_[position_];
      if (c == '>') {
        position_++;
        break;
      }
      if (c == '/') {
        token.selfClosing = true;
        position_++;
        continue;
      }
      readAttribute(token);
    }
    return token;
  }

  void readAttribute(Token &token) {
    size_t start = position_;
    while (position_ < html_.size()) {
      char c = html_[position_];
      if (isWhitespace(c) || c == '=' || c == '>' || c == '/') break;
      position_++;
    }
    std::string name = toLower(html_.substr(start, position_ - start));
    if (name.empty()) {
      position_++;
      return;
    }
    skipWhitespace();
    std::string value;
    if (position_ < html_.size() && html_[position_] == '=') {
      position_++;
      skipWhitespace();
      if (position_ < html_.size() && (html_[position_] == '"' || html_[position_] == '\'')) {
        char quote = html_[position_++];
        size_t begin = position_;
        while (position_ < html_.size() && html_[position_] != quote) position_++;
        value = html_.substr(begin, position_ - begin);
        if (position_ < html_.size()) position_++;
      } else {
        size_t begin = position_;
        while (position_ < html_.size() && !isWhitespace(html_[position_]) && html_[position_] != '>') position_++;
        value = html_.substr(begin, position_ - begin);
      }
    }
    token.attrs.push_back({name, decodeEntities(value)});
  }

  Token readEndTag() {
    Token token;
    token.kind = TokenKind::End;
    position_ += 2;  // consume '</'
    token.name = readName();
    size_t end = html_.find('>', position_);
    position_ = (end == std::string::npos) ? html_.size() : end + 1;
    return token;
  }

  void readRawText(const std::string &tag, bool keepText, std::vector<Token> &tokens) {
    size_t start = position_;
    std::string close = "</" + tag;
    while (position_ < html_.size()) {
      if (html_[position_] == '<' && toLower(html_.substr(position_, close.size())) == close) {
        break;
      }
      position_++;
    }
    std::string raw = html_.substr(start, position_ - start);
    if (keepText && !raw.empty()) {
      Token text;
      text.kind = TokenKind::Text;
      text.name = raw;
      tokens.push_back(std::move(text));
    }
    if (position_ < html_.size()) {
      Token end = readEndTag();
      tokens.push_back(std::move(end));
    }
  }
};

/* -- tree builder --------------------------------------------------------- */

namespace {

bool isBlockTag(const std::string &tag) {
  static const std::unordered_map<std::string, bool> set = {
      {"address", true}, {"article", true}, {"aside", true},  {"blockquote", true},
      {"div", true},     {"dl", true},      {"fieldset", true}, {"figure", true},
      {"footer", true},  {"form", true},    {"h1", true},       {"h2", true},
      {"h3", true},      {"h4", true},      {"h5", true},       {"h6", true},
      {"header", true},  {"hr", true},      {"main", true},     {"nav", true},
      {"ol", true},      {"p", true},       {"pre", true},      {"section", true},
      {"table", true},   {"ul", true},
  };
  return set.count(tag) != 0;
}

bool isHeading(const std::string &tag) {
  return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

/** Whether a start tag implicitly closes the currently open element. */
bool closesOpenElement(const std::string &open, const std::string &next) {
  if (open.empty()) return false;
  if (next == "li" && open == "li") return true;
  if ((next == "dt" || next == "dd") && (open == "dt" || open == "dd")) return true;
  if (next == "option" && open == "option") return true;
  if (next == "p" && open == "p") return true;
  if (isHeading(next) && (isHeading(open) || open == "p")) return true;
  if (isBlockTag(next) && open == "p") return true;
  if ((next == "td" || next == "th") && (open == "td" || open == "th")) return true;
  if (next == "tr" && (open == "td" || open == "th" || open == "tr")) return true;
  if ((next == "thead" || next == "tbody" || next == "tfoot") &&
      (open == "td" || open == "th" || open == "tr" || open == "thead" ||
       open == "tbody" || open == "tfoot")) {
    return true;
  }
  return false;
}

}  // namespace

std::unique_ptr<Node> xt_html_parse(const std::string &html) {
  Tokenizer tokenizer(html);
  std::vector<Token> tokens = tokenizer.run();

  auto document = std::make_unique<Node>(NodeType::Document);
  std::vector<Node *> stack;
  stack.push_back(document.get());

  for (Token &token : tokens) {
    if (token.kind == TokenKind::Text) {
      Node *current = stack.back();
      /* Text inside <style>/<title> is kept verbatim; elsewhere keep it too so
       * layout can collapse whitespace later. */
      current->addText(token.name);
      continue;
    }
    if (token.kind == TokenKind::Start) {
      while (stack.size() > 1 && closesOpenElement(stack.back()->tag, token.name)) {
        stack.pop_back();
      }
      Node *element = stack.back()->addElement(token.name);
      element->attrs = std::move(token.attrs);
      bool isVoid = voidElements().count(token.name) != 0;
      if (!isVoid && !token.selfClosing) stack.push_back(element);
      continue;
    }
    if (token.kind == TokenKind::End) {
      for (size_t i = stack.size(); i-- > 1;) {
        if (stack[i]->tag == token.name) {
          stack.resize(i);
          break;
        }
      }
    }
  }

  return document;
}

/* -- serialization (diagnostics) ------------------------------------------ */

static void serialize(const Node *node, std::string &out) {
  if (node->type == NodeType::Document) {
    for (const std::unique_ptr<Node> &child : node->children) serialize(child.get(), out);
    return;
  }
  if (node->type == NodeType::Text) {
    out += trim(node->text);
    return;
  }
  out += "<" + node->tag;
  for (const Attr &attr : node->attrs) out += " " + attr.name + "=\"" + attr.value + "\"";
  out += ">";
  for (const std::unique_ptr<Node> &child : node->children) serialize(child.get(), out);
  out += "</" + node->tag + ">";
}

std::string xt_dom_to_string(const Node *node) {
  std::string out;
  if (node != nullptr) serialize(node, out);
  return out;
}

std::string xt_dom_describe(const Node *node) {
  if (node == nullptr) return "";
  if (node->isText()) return "#text";
  if (!node->isElement()) return "#document";
  std::string out = node->tag;
  const std::string *id = node->attr("id");
  if (id != nullptr && !id->empty()) out += "#" + *id;
  const std::string *classes = node->attr("class");
  if (classes != nullptr) {
    size_t index = 0;
    while (index < classes->size()) {
      while (index < classes->size() && std::isspace((unsigned char)(*classes)[index])) index++;
      size_t start = index;
      while (index < classes->size() && !std::isspace((unsigned char)(*classes)[index])) index++;
      if (index > start) out += "." + classes->substr(start, index - start);
    }
  }
  return out;
}

}  // namespace xtgui
