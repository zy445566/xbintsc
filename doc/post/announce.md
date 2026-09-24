# Guys, TypeScript can now be compiled into native binaries

## Background

Back in 2019 I spent some time studying compilers. I wrote a mini JS compiler that only supported the `function` and `number` types — but don't underestimate just those two types: it took me nearly two weeks to implement. That's when I realized a compiler demands an enormous amount of time and effort, and I sighed that I would probably never manage to build a relatively complete one in my lifetime.

Then one afternoon in September 2026, someone in a group chat remarked that frontend / JS / TS will always sit at the very bottom of the food chain, because unlike C++ and Rust it can't produce binary files.

## The Trigger

It was like something in my core got triggered. A thought kept flooding my mind, so I went back and dug up the articles I had written about compilers back then. No! That's not right! If using Ai,it might actually be possible now. So I started laying out my ideas and wrote my first document, DESIGN.md. Then I opened pi and started hacking!

Very quickly — in under an hour — the first prototype appeared. Looking at how ugly and simplistic it was, not even as good as the demo I wrote years ago, I decided to rewrite the entire framework from scratch. After several days I finally reached the level I wanted. Then I started a second attempt: I fired up pi again, and this time I found that although the AI was only so-so at architecture, once the architecture was in place it completed features unbelievably fast. So I kicked off a rapid iteration process.

## Core Progress

* The vast majority of JS/TS syntax plus mainstream Node.js APIs (as optional packages)
* Support for C++ and Rust extensions, so users can freely extend it
* Self-hosting achieved — it compiles itself
* Runs with no GNU dependency

Of course, what's not implemented is listed here:
[unimplemented.md](https://github.com/zy445566/xbintsc/blob/main/doc/unimplemented.md)
[node-unimplemented.md](https://github.com/zy445566/xbintsc/blob/main/doc/node-unimplemented.md)

The biggest advantages of binary compilation come down to two things:
* Tiny bundle size — the compiled binary is as small as 200 KB (--ext node 300KB) and runs directly, with no need to bundle a tens-of-MB Node.js runtime anymore
* Blazing-fast cold start — measured on my machine at roughly 120–200× faster than native Node.js

## Usage

Download the zst archive from the [releases page](https://github.com/zy445566/xbintsc/releases), extract it, and run it. Here's a Windows example:
```
# Build
.\xbintsc-win32-x64\bin\xbintsc.exe build .\hello.ts --out .\build
# Run
.\build\hello.exe
```

# Appendix

GitHub: [https://github.com/zy445566/xbintsc](https://github.com/zy445566/xbintsc)
If you're interested, let's dig into it together `;)`
