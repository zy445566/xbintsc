# Compilation pipeline

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/pipeline.md)

```
source.ts
   │  lexer       src/lexer/scanner.ts + token.ts
   ▼
 tokens
   │  parser      src/parser/parser.ts  →  AST  src/ast/
   ▼
 AST
   │  binder      src/binder/binder.ts  →  scopes/symbols/closure capture
   ▼
 bound AST
   │  codegen     src/codegen/llvm.ts + values.ts  →  LLVM IR text
   ▼
 module.ll ── clang ──► module.o ──link──► executable
                                   ▲
                          runtime/*.c (C runtime, split by function)
```

- Front end, back end, runtime and extensions are fully decoupled.
- The flow is orchestrated by `src/driver/compiler.ts`: `read → parse → bind → IR → object file → link`. The front end lives in `src/driver/frontend.ts`, the cache key in `src/driver/build-key.ts`, runtime compilation in `src/driver/runtime-objects.ts`, and platform artifacts in `src/driver/app-artifacts.ts`.
- **Self-hosting**: `xbintsc` compiles its own front end. Building `src/cli/main.ts`
  with the compiler yields a working `xbintsc` binary, and that binary rebuilds
  itself with byte-identical LLVM IR (source ≡ generation 1 ≡ generation 2 ≡
  generation 3). The runtime is still hand-written C.
