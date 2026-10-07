# 计划：仅 `node_modules` 支持 `require`（CommonJS 兼容）

> 状态：**部分实施**（0.4.x）。实际落地采用**静态降级**而非本文 4.4/4.5 的运行时工厂/注册表方案：
> 每个 CJS 模块在打包阶段注入 `const <prefix>$cjs_module = { exports: {} }` 与 `<prefix>$cjs_exports`，
> `require("pkg")` 直接改写为依赖的 `$cjs_module.exports`，导出名通过末尾的快照常量暴露给 ESM。
> 已实现：解析器修复（P0）、`node_modules` 内 CJS→CJS / CJS→ESM、ESM→CJS 的 default/named、
> 外部/内置 `require` 提升为 ESM import、非字面量 `require` 诊断。
> 未实现（相对本文的差异）：循环 `require` 的部分导出时序、懒执行、`require.resolve` / `require.cache`、
> `__dirname` / `__filename`、`Object.defineProperty` getter 形式。详见下文 4.x 与 `unimplemented.md`。
>
> 目标版本：0.4.x
> 关联文档：[unimplemented.md](./unimplemented.md) 第 4 节（模块系统）、[implemented.md](./implemented.md) 第 3.4 节

## 1. 背景与现状

xbintsc 目前在 **driver 层做源码级打包**（`src/driver/bundler/`）：

```
entry.ts
  │ loadGraph       解析并绑定 entry + 所有可达模块（import/export）
  ▼
ModuleRecord[]     （依赖在前，入口在最后）
  │ merge.ts        按模块前缀重命名顶层符号、改写引用、合并成单个 SourceFile
  ▼
merged SourceFile  →  bind → codegen(LLVM IR)  →  clang  →  原生二进制
```

现状约束：

- 只支持 ESM：`import` / `export`，`node_modules` 里**只支持 ESM 包**。
- `require()` 在 codegen 被显式拒绝：
  - `src/codegen/generator/calls/invocation.ts`（`require("x")` 调用）
  - `src/codegen/generator/expressions/primary.ts`（裸 `require` 引用）
  - `src/codegen/generator/context.ts` → `reportRequireUse()`
- `src/driver/bundler/resolve.ts` 已经能把裸 specifier 解析到 `node_modules`（含 `exports` / `module` / `main` / scoped / 子路径），但解析出的 CJS 源码在 codegen 阶段会因为 `require` 而失败。
- `resolve.ts` 的 `EXPORT_CONDITIONS` 固定为 `["import", "module", "default", "node", "require"]`：对 `require` 站点会错误地优先选中 ESM 入口。
- `isModule` 判定（`compiler.ts`）只看 entry 是否有 `import`/`export`/`export =`；只有 `require` 的入口不会触发打包。
- **解析器缺口**：`module` 是上下文关键字，但 `parseStatement` 对 `ModuleKeyword` 直接进入 `parseModuleDeclaration`，导致 CJS 常见的 `module.exports = ...`（语句首）解析失败：

  ```
  module.exports.b = 2;
  ^^^^^^ error TS2003 / TS4002
  ```

  实测确认（`src/parser/statements.ts:198`、`:231`）。参数位置使用 `module` 是合法的，`module.exports` 作为表达式也合法，问题只在语句首的判定。

## 2. 目标与非目标

### 目标

1. `require("pkg")` 在 specifier **解析后位于任意 `node_modules/` 目录内**时可用（入口用户代码、以及 CJS 包内部都算）。
2. 支持 CJS 包内部的相对 `require("./util")`（这类路径同样落在 `node_modules` 内）。
3. 支持 `module.exports`、`exports.x = ...`、懒加载、按模块缓存、循环 `require` 的部分导出语义。
4. ESM `import` 一个 CJS 包时提供 default / named / namespace 互操作。
5. 二进制保持**自包含**：编译期完成打包与解析，运行期不访问 `node_modules`、不做文件系统解析。

### 非目标（本期不做，或另立计划）

- 用户自有代码里的**相对** `require("./local")` 仍报错（仅 `node_modules` 开放）。
- 任意动态 `require(expr)`：只静态解析**字符串字面量**；动态调用保留运行期兜底（可能在已知依赖里查找，否则抛错）。
- 完整 Node CJS 宿主语义：`global` / `process` / `Buffer` / `module.parent` / `require.resolve` / `require.cache` / `__dirname` 的完整行为。
- `require` ESM 包（Node ≥22 的 `require(esm)`）——列为可选 Phase。
- 循环 ESM、live binding（沿用现状）。
- JSON 模块（`require("./x.json")`）——列为可选 Phase。

## 3. 核心规则（一句话）

> **只有当 `require(spec)` 的目标路径位于某个 `node_modules/` 目录内时才允许；否则维持现有诊断（引导改用 `import`）。**

规则作用于 **解析后的目标位置**，而不是调用方文件位置。由此自然得到：

- 入口 `const _ = require("lodash")` ✅（lodash 在 node_modules）
- CJS 包内 `require("./util")` ✅（util 在 node_modules 包目录内）
- 用户 `require("./local")` ❌（不在 node_modules）
- 用户 `require("/abs/path")` ❌

## 4. 设计

### 4.1 P0 解析器修复：语句首的 `module` / `namespace`

`src/parser/statements.ts` 的两处 `case TokenKind.ModuleKeyword:` / `NamespaceKeyword`：

只有当下一个 token 是**标识符类**（`Foo`）或字符串字面量（`declare module "x"`）时才进入 `parseModuleDeclaration`；否则 `break` 落到 `parseExpressionStatement`，让 `module` 作为普通标识符参与表达式。

```ts
case TokenKind.ModuleKeyword:
case TokenKind.NamespaceKeyword: {
  const next = this.lookAhead(1);
  if (this.isIdentifierLike(next) || next.kind === TokenKind.StringLiteral) {
    return this.parseModuleDeclaration(...);
  }
  break; // module.exports / module["x"] / module(...)
}
```

> 该修复独立、低风险，应最先合入；否则任何 CJS 包都无法解析。

### 4.2 模块格式判定（ESM vs CJS）

给 `ModuleRecord` 增加 `format: "esm" | "cjs"`、`isNodeModules: boolean`、`cjsId?: string`。

判定顺序（`loadGraph` 内，`src/driver/bundler/graph.ts`）：

1. 扩展名：`.mjs` / `.mts` → ESM；`.cjs` / `.cts` → CJS。
2. 向上查找最近的 `package.json` 的 `"type"`：
   - `"module"` → ESM；`"commonjs"` / 缺省 → CJS。
3. 兜底嗅探：顶层出现 `module.exports` / `exports.` / `require(` 且没有 ESM `import`/`export` → CJS。

`node_modules` 内默认（无 `"type"`）即 CJS，这与 Node 一致。

### 4.3 require 解析与导出条件

`src/driver/bundler/resolve.ts`：

- `classifyDependency` / `resolveNodePackage` 增加一个 `conditions`（`"import" | "require"`）参数。
- 从 CJS 模块或 `require(...)` 站点解析时，条件优先级改为
  `["require", "node", "default"]`，并优先 `main` 而非打包器字段 `module`。
- 从 ESM `import(...)` 解析时保持现状 `["import", "module", "default", "node", "require"]`。
- 解析结果附带 `isNodeModules`（路径包含 `/node_modules/` 段）。

### 4.4 CJS 打包模型：工厂 + 注册 + 懒加载

对每个 CJS 模块，在 merge 阶段把它的顶层语句包进一个工厂函数：

```js
function <prefix>require(spec) {
  const target = <prefix>deps[spec];
  if (target === undefined) throw new Error("Cannot find module '" + spec + "'");
  return __xbintsc_cjs_load(target);
}

__xbintsc_cjs_register(
  "<prefix>",
  function (module, exports, require, __filename, __dirname) {
    /* 原 CJS 顶层语句（已按 <prefix> 重命名） */
    /* ………………………………………………………………………… */
  },
  <prefix>require,
);
```

要点：

- 工厂参数 `module` / `exports` / `require` / `__filename` / `__dirname` 在 codegen 的**重新 bind**（`GeneratorContext` 构造时 `bind(sourceFile)`）时会解析为函数参数，不会触发 `CannotFindName`。
- 顶层 `var` / `function` 进入工厂后天然变成模块私有，符合 CJS 语义。
- `<prefix>deps` 是 `{ "./util": "m7", "dep": "m3" }` 形状的对象字面量，**编译期**由 `classifyDependency` 解析生成（因此运行期不需要 fs）。
- 依赖收集：遍历每个模块 AST 里 `require("字面量")` 的 `CallExpression`（callee 为无 symbol 的标识符 `require`），解析并写入 deps 表。
- 工厂注册语句统一放在合并产物的**最前面**（注册只是赋值，顺序无关），然后才是各模块语句；这样任何 `__xbintsc_cjs_load` 调用前注册都已就绪。
- 循环 require：`load` 在**执行工厂前**就把 `module.exports` 放进缓存，保证部分导出可见，与 Node 一致。

### 4.5 预置加载器（运行时垫片）

优先方案：把加载器作为**一段 JS/TS 源码字符串**注入合并产物（解析成 AST 语句），命名统一加 `__xbintsc_cjs_*` 前缀，避开用户符号。

```js
var __xbintsc_cjs_cache = {};
var __xbintsc_cjs_factories = {};
var __xbintsc_cjs_requires = {};

function __xbintsc_cjs_register(id, factory, requireFn) {
  __xbintsc_cjs_factories[id] = factory;
  __xbintsc_cjs_requires[id] = requireFn;
}

function __xbintsc_cjs_load(id) {
  var cached = __xbintsc_cjs_cache[id];
  if (cached !== undefined) return cached;
  var factory = __xbintsc_cjs_factories[id];
  if (factory === undefined) throw new Error("Cannot find module '" + id + "'");
  var module = { exports: {} };
  __xbintsc_cjs_cache[id] = module.exports;      // 先入缓存，支持循环 require
  factory(module, module.exports, __xbintsc_cjs_requires[id], id, "");
  __xbintsc_cjs_cache[id] = module.exports;      // 处理 module.exports 重赋值
  return module.exports;
}
```

- 该子集（对象字面量、元素访问、函数值调用、`throw new Error`、闭包）现有 codegen 全部支持，**无需改 C runtime**。
- 备选方案：在 C runtime 增加 `xt_cjs_register` / `xt_cjs_load`（性能更好、产物更干净），但需要把工厂作为 `xt_value` 回调用通用 ABI 调用，工作量和风险更高。**建议先走源码垫片**，后续需要再下沉到 C。

### 4.6 ESM ↔ CJS 互操作

在 `merge.ts` Phase 2/3 增加分支：

- 依赖为 CJS 时，不再扫描 `dependency.exports`（为空），而是：
  1. 在依赖顺序位置插入 `const <prefix>exports = __xbintsc_cjs_load("<prefix>");`（只对**被 ESM import 引用**的 CJS 模块做急切加载；纯 `require` 的仍懒加载）。
  2. `import d from "cjs"` → 引用改写为 `<prefix>exports`；若 `exports.__esModule` 为真则取 `exports.default`（运行期判定，可用一个小 helper）。
  3. `import { a, b as c } from "cjs"` → 生成 `const <prefix>a = <prefix>exports.a;` 之类的快照常量，引用改名到该常量（与现有 ESM 快照语义一致）。
  4. `import * as ns from "cjs"` → 命名空间直接用 `<prefix>exports`。
- `export { x } from "cjs"` / `export * from "cjs"` 同理。
- CJS 内部 `require("path")` 等**扩展模块**：deps 表把 specifier 映射到扩展模块的命名空间对象（复用 `extension registry` 的 exports），Phase 3 可选支持。

### 4.7 `require` 调用点重写

在 merge 的最终阶段，遍历所有模块（含入口）：

- `require("literal")` 且目标是 **已打包的 CJS 模块** → 重写为 `__xbintsc_cjs_load("<id>")`（入口/ESM 中直接内联；CJS 工厂内因为工厂参数已叫 `require`，可直接保留调用，由传入的 `<prefix>require` 承担查找）。
- 目标是 **ESM 模块** → 本期报“require 不支持 ESM”，或列为可选 Phase 支持。
- 目标是 **扩展模块 / node 内置模块** → 可选：转成命名空间对象。
- **不在 node_modules** → 保留（更清晰的）诊断：`CommonJS \`require()\` is only supported for packages under node_modules; use an ESM import`。
- 非字面量参数 → 保留裸 `require` 调用；运行时由 `<prefix>require` 兜底抛错（并在有把握时给出诊断）。

codegen 里现有的裸 `require` 诊断（`reportRequireUse`）保留为**兜底**：只有未被 bundler 重写的 `require` 才会走到那里。

### 4.8 触发打包的条件

`compiler.ts` 的 `isModule` 判定扩展为：

```
hasModuleSyntax(entry)  ||  entryAstContainsRequire(entry)
```

即入口只有 `require("pkg")`（没有 import/export）也要触发 `bundleModules`，否则 `require` 不会进入打包流程。`loadGraph` 同步支持从 `require` 字面量递归加载依赖。

## 5. 分阶段实施

| 阶段 | 内容 | 交付/验证 |
| --- | --- | --- |
| **P0** | 解析器修复：语句首 `module`/`namespace` 判定（4.1） | 新增 parser 单测：`module.exports.x = 1;`、`namespace.foo()` 可解析 |
| **P1** | 格式判定 + `isNodeModules` + `conditions` 参数（4.2/4.3） | `bundler-resolve.test.ts` 覆盖 cjs/esm 判定、`require` 条件、`type` 字段 |
| **P2** | loadGraph 收集 `require` 依赖 + `isModule` 扩展（4.8） | 入口仅 `require("pkg")` 也能打包；相对 require 仍报错 |
| **P3** | CJS 工厂包装 + 注册表 + 预置加载器（4.4/4.5） | e2e：`module.exports`、`exports.x`、嵌套相对 require、循环 require、重赋值 |
| **P4** | require 调用点重写 + 诊断（4.7） | 单测：node_modules 内重写为 `__load`，外部保持报错 |
| **P5** | ESM→CJS 互操作 default/named/namespace（4.6） | 差分测试：与 Node 输出一致（default 函数、命名导出、`__esModule`） |
| **P6** | 扩展模块 / 可选：`require(esm)`、JSON 模块 | 按需 |
| **P7** | 文档更新：implemented / unimplemented 第 4 节、README | 文档评审 |

每个阶段都应保持现有测试全绿；P0 单独可合入。

## 6. 涉及文件

| 文件 | 改动 |
| --- | --- |
| `src/parser/statements.ts` | 4.1 语句首 `module`/`namespace` 判定 |
| `src/driver/bundler/types.ts` | `ModuleRecord.format` / `isNodeModules` / `cjsId`；`BundleResult` 可能需要暴露统计 |
| `src/driver/bundler/resolve.ts` | `conditions` 参数、`isNodeModules`、require 条件优先级 |
| `src/driver/bundler/graph.ts` | 格式判定、收集 `require` 依赖、递归加载 |
| `src/driver/bundler/merge.ts` | CJS 工厂包装、注册/加载语句注入、require 重写、互操作、依赖表 |
| `src/driver/bundler/cjs.ts`（新增） | 加载器源码字符串 + 工厂/依赖表/DepsMap 的 AST 构造 helper |
| `src/driver/compiler.ts` | `isModule` 扩展（4.8），`cacheText` 覆盖新注入代码 |
| `src/codegen/generator/context.ts` | `reportRequireUse` 文案更新（node_modules 限定） |
| `tests/driver/bundler-resolve.test.ts` | 解析/格式/条件单测 |
| `tests/driver/bundler-cjs.test.ts`（新增） | 工厂包装与重写单测 |
| `tests/e2e/npm-modules.test.ts` | CJS 包 e2e + 差分 |
| `tests/parser/*` | P0 回归 |
| `doc/implemented.md`、`doc/unimplemented.md`、`doc/zh-CN/*` | 第 4 节状态更新 |

## 7. 测试计划

- **单测**
  - 解析器：`module.exports`、`module.exports.fn = ...`、`exports.x`、`module["exports"]`。
  - 解析：`.cjs`/`.mjs`、`package.json#type`、无 type 默认 CJS、`require` 条件选 `main`、`import` 条件选 `exports.import`。
  - 打包：工厂签名、deps 表内容、require 重写、非 node_modules 相对 require 报错。
- **e2e（与 Node 差分）**
  - `module.exports = function`（默认函数导出）。
  - `exports.a` / `exports.b` 命名导出。
  - CJS 内部相对 `require("./util")`、二级依赖。
  - 循环 require 的部分导出。
  - `module.exports` 在工厂执行中重赋值。
  - ESM `import make, { add } from "cjs-pkg"` 差分。
  - `import * as ns`。
  - 入口仅 `require("pkg")` 无 import。
  - 回归：`import` ESM 包仍正常；用户 `require("./local")` 仍报错。
- **CI**：沿用 `describeE2E` 的 clang 存在性跳过逻辑；不得引入 Node 运行期依赖。

## 8. 风险与取舍

| 风险 | 说明 / 缓解 |
| --- | --- |
| 动态 `require(expr)` | 无法静态打包；运行时兜底抛错，文档明确说明 |
| 依赖宿主对象（`process`、`Buffer`、`global`） | 不在本期范围；这类包会报 `Cannot find name`，文档列出边界 |
| 命名导入的静态分析 | 不做 `cjs-module-lexer`；用运行期属性 + 快照常量，语义与现有 ESM 快照一致 |
| `resolve.ts` 现有 `module` 字段偏好 | 仅对 `import` 保持；`require` 站点改走 `main`/`require` 条件，避免破坏现有 ESM 行为 |
| 注入代码与缓存 | 注入的加载器/工厂必须计入 `cacheText` 与缓存指纹，否则增量编译会复用旧产物 |
| 注入源码的符号冲突 | 统一 `__xbintsc_cjs_*` 前缀；用户顶层符号已全部带 `mN_` 前缀，冲突概率极低 |
| 与 `export =` 的关系 | 现有 `ExportAssignment`（`export =`）先不动；CJS 判定以此为准另行处理，避免回归 |

## 9. 验收标准

1. 入口 `const fn = require("some-cjs-pkg"); fn();` 能编译运行且与 Node 输出一致。
2. CJS 包内部相对 `require`、嵌套依赖、循环依赖均与 Node 行为一致。
3. ESM `import` CJS 包的 default / named / namespace 与 Node 一致。
4. 用户自有代码的相对 `require("./x")` 仍给出清晰错误。
5. 现有 ESM `node_modules` 与所有测试保持通过。
