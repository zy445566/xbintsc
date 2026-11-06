/*
 * xbintsc GUI engine — WGSL abstract syntax tree.
 *
 * The tree is deliberately small: it models the WGSL a WebGPU renderer writes
 * (entry points, structs, resources, functions, the usual statements) and
 * nothing else. Types are shared because a struct member and a variable can
 * name the same struct, and because array element types nest.
 */
#ifndef XT_GUI_WGSL_AST_H
#define XT_GUI_WGSL_AST_H

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace xtgui {
namespace wgsl {

enum class Scalar { Bool, I32, U32, F32 };
enum class Kind {
  Unknown,
  Void,
  Scalar,
  Vector,
  Matrix,
  Array,
  Struct,
  Atomic,
  Ptr,
  Sampler,
  SamplerComparison,
  Texture,        // sampled texture
  StorageTexture  // write/read storage texture
};

enum class TextureDim { D1, D2, D2Array, D3, Cube, CubeArray, Multisampled2D };

enum class ImageFormat {
  Unknown,
  Rgba8Unorm,
  Rgba8Snorm,
  Rgba8Uint,
  Rgba8Sint,
  Rgba16Float,
  Rgba32Float,
  R32Float,
  R32Uint,
  R32Sint,
  Rgba16Uint,
  Rgba16Sint,
  Bgra8Unorm
};

struct StructDecl;
struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
  Kind kind = Kind::Unknown;
  Scalar scalar = Scalar::F32;
  /** Vector width (1 for scalars). */
  int width = 1;
  /** Matrix columns/rows. */
  int cols = 1;
  int rows = 1;
  TypePtr elem;
  /** Array length; 0 means a runtime-sized array. */
  int length = 0;
  const StructDecl *strct = nullptr;
  /** Texture shape and storage format. */
  TextureDim dim = TextureDim::D2;
  ImageFormat format = ImageFormat::Unknown;
  bool depth = false;
  /** 1 = read, 2 = write, 3 = read_write (storage textures/buffers). */
  int access = 1;

  bool is_scalar() const { return kind == Kind::Scalar; }
  bool is_vector() const { return kind == Kind::Vector; }
  bool is_numeric() const { return kind == Kind::Scalar || kind == Kind::Vector; }
};

enum class Space { None, Private, Workgroup, Uniform, Storage };

struct Attribute {
  std::string name;
  std::vector<int> ints;
  std::vector<std::string> words;
};

struct Member {
  std::string name;
  TypePtr type;
  int location = -1;
  std::string builtin;
  std::string interpolate;
  int align = -1;
  int size = -1;
};

struct StructDecl {
  std::string name;
  std::vector<Member> members;
};

struct Param {
  std::string name;
  TypePtr type;
  int location = -1;
  std::string builtin;
  std::string interpolate;
};

/** A literal, name, operator, call, member access, index or array ctor. */
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Expr {
  enum class K {
    Int,
    Float,
    Bool,
    Ident,
    Unary,
    Binary,
    Call,
    Member,
    Index,
    ArrayCtor,
    Bitcast
  } kind = K::Ident;
  std::string text;   // literal spelling, identifier, operator or callee
  std::string member; // `.member`
  std::vector<ExprPtr> args;
  TypePtr type;       // array constructor / bitcast target type
  int line = 0;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Stmt {
  enum class K {
    Block,
    Var,
    Assign,
    Expr,
    If,
    For,
    While,
    Loop,
    Break,
    Continue,
    Return,
    Discard,
    Switch
  } kind = K::Block;

  std::vector<StmtPtr> body;          // Block / Loop / If-then / SwitchCases
  std::vector<StmtPtr> otherwise;     // else branch
  StmtPtr init, cond, update;         // For
  ExprPtr expr;                       // If/While/For cond, Return, Expr, Assign
  ExprPtr value;                      // Assign value
  std::string op;                     // Assign operator (`=`, `+=`, …)
  std::string name;                   // Var name
  TypePtr type;                       // Var type
  Space space = Space::None;          // Var address space
  std::string access;                 // storage access (`read`, `write`, `read_write`)
  bool is_const = false;              // `let`/`const`
  StmtPtr continuing;                 // Loop `continuing` block
  std::vector<ExprPtr> cases;         // Switch case selectors (null = default)
  std::vector<StmtPtr> case_bodies;   // parallel to cases
  int line = 0;
};

struct FuncDecl {
  std::string name;
  std::vector<Param> params;
  TypePtr ret;
  std::vector<StmtPtr> body;
  bool is_entry = false;
  std::string stage;      // `vertex`, `fragment` or `compute`
  std::string ret_builtin;
  int ret_location = -1;
  std::string ret_interpolate;
  int workgroup[3] = {1, 1, 1};
  bool has_workgroup = false;
};

struct GlobalDecl {
  std::string name;
  TypePtr type;
  Space space = Space::Private;
  std::string access;
  int group = -1;
  int binding = -1;
  ExprPtr init;
  int line = 0;
};

struct Module {
  std::vector<std::string> enables;
  /* A deque: types hold `StructDecl*`, and the address must survive later
   * struct declarations in the same module. */
  std::deque<StructDecl> structs;
  std::vector<GlobalDecl> globals;
  std::vector<FuncDecl> funcs;
  std::vector<std::pair<std::string, TypePtr>> aliases;
};

/** Parse `source`; on failure `error` receives a `line:col: message`. */
bool parse_module(const std::string &source, Module *out, std::string *error);

/* -- type helpers (wgsl_type.cpp) ----------------------------------------- */

TypePtr make_scalar(Scalar scalar);
TypePtr make_vector(Scalar scalar, int width);
TypePtr make_matrix(Scalar scalar, int cols, int rows);
TypePtr make_array(TypePtr elem, int length);
std::string scalar_glsl(Scalar scalar);
/** The GLSL base type (`float`, `vec3`, `mat4`, `S`, `sampler2D`, …). */
std::string glsl_type(const Type &type);
/** A declaration: `vec3 name`, `float name[4]`. */
std::string glsl_decl(const Type &type, const std::string &name);
/** Human-readable WGSL type name, for diagnostics. */
std::string wgsl_type(const Type &type);
/** The constructed type named `name` (`vec3f`, `mat4x4f`, `S`), or null. */
TypePtr type_by_name(const Module &module, const std::string &name);
/** GLSL image format for a storage texture's `rgba8unorm`-style suffix. */
std::string image_format_glsl(ImageFormat format);

}  // namespace wgsl
}  // namespace xtgui

#endif /* XT_GUI_WGSL_AST_H */
