# xbintsc for AI agents

**xbintsc compiles a subset of TypeScript straight to native binaries.** It
parses TypeScript itself, lowers the program to LLVM IR text, and invokes
`clang` to produce a standalone executable linked against a small C runtime
(`runtime/`). The output does not embed Node.js or a TypeScript compiler.

```
source.ts --lexer--> tokens --parser--> AST --binder--> bound AST
          --codegen--> module.ll --clang--> module.o --link--> executable
```

This directory is the **AI-facing, task-oriented** layer of the documentation.
It is deliberately short and links onward; the canonical detail lives in the
documents it points at. Read the page for your task, not the whole directory.

| Page | Use it when |
| --- | --- |
| [build-recipe.md](./build-recipe.md) | You need to compile or run a program, and want the exact command shape |
| [cli.md](./cli.md) | You need the full flag list, project config, or the programmatic API |
| [language-support.md](./language-support.md) | You must know whether a syntax feature or API exists, or behaves like Node |
| [extensions.md](./extensions.md) | You need `fs`/`http`/…, a GUI, or a C++/Rust library |
| [troubleshooting.md](./troubleshooting.md) | Something failed and you need the cause and the fix |

New to this repository (rather than to a program that uses xbintsc)? Read
[../AGENTS.md](../../AGENTS.md) first — it covers the layout, the build gate and
the rules for changing the compiler.

## The three things that decide everything

1. **Only a subset of TypeScript compiles.** Unsupported syntax is rejected, not
   approximated. Check before you write a lot of code.
2. **Types are erased, never checked.** Annotations, interfaces, generics,
   `as`/`satisfies` and non-null `!` have no runtime effect, and no type checker
   runs. Only real runtime behaviour matters.
3. **Node compatibility is opt-in and incomplete.** Node modules come from the
   `node` extension (`--ext node`); bare third-party npm imports are not
   supported.

## Choose your command

```bash
# Run a program now (compiles to a temp binary, then executes it)
xbintsc run app.ts

# Produce a standalone binary
xbintsc build app.ts --out build          # -> build/app[.exe]

# See the LLVM IR that would be compiled
xbintsc emit app.ts

# Ask what toolchain xbintsc resolved
xbintsc doctor
```

In a source checkout, replace `xbintsc` with `npx tsx src/cli/main.ts` (or run
`npm run xbintsc --`). A released archive needs no Node.js.

Next: [build-recipe.md](./build-recipe.md) for the working commands,
[language-support.md](./language-support.md) before writing a nontrivial
program.

> 中文版本：[zh-CN/](./zh-CN/)
