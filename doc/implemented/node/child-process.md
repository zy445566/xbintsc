# `child_process`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/child-process.md)

Location: `src/extensions/node/child_process/index.ts`, `runtime/ext_node/child_process/child_process.c`

`spawnSync(command, args[, options])` runs a program to completion and returns
`{ status, stdout, stderr }`. `options.cwd` sets the working directory,
`options.env` replaces the child's environment, and `options.stdio: "inherit"`
hands the child the parent's stdout/stderr (used by `xbintsc run`, so a
compiled program's output streams live); otherwise stdout/stderr are captured
as UTF-8 strings.

| Option | Notes |
| --- | --- |
| `cwd` | working directory for the child |
| `env` | the child's complete environment; omit it to inherit, spread `process.env` into it to extend |
| `stdio: "inherit"` | share the parent's stdout/stderr instead of capturing |
| `encoding` | accepted and ignored (output is always decoded as UTF-8) |

`env` follows Node's replace semantics: a present object *is* the environment,
so a variable the caller does not list is absent in the child. That is what lets
a caller hand a toolchain a specific `SDKROOT`, `INCLUDE` or `LIB` without
leaking its own shell setup — and what the macOS SDK fix relies on, because a
self-hosted compiler runs this same code.

On Windows the child is launched with `CreateProcess` and its stdout/stderr are
drained through anonymous pipes by reader threads. No temporary files are used,
so a locked-down `%TEMP%` cannot break `spawnSync`, and output larger than a pipe
buffer cannot deadlock.

```ts
import { spawnSync } from "child_process";

const result = spawnSync("clang", ["--version"], { encoding: "utf8" });
console.log(result.status, result.stdout.split("\n")[0]);
```
