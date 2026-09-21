# xbintsc 自包含路线图（无需用户预装外部工具）

> 语言 / Language：[English](../self-contained-roadmap.md) | **简体中文**

目标是让终端用户**只安装 xbintsc 本身**就能完成 `build` / `run`，不再需要自行
安装 `clang` / `gcc` / `ld` / `llc` 等编译链接工具。

> **已记录的决策：**
> - **macOS**：预期用户安装 **Xcode Command Line Tools**。因此 xbintsc 在 macOS 上
>   直接使用系统 clang / 链接器 / SDK，**不**随包分发工具链。见
>   [`requirements.md`](./requirements.md)。
> - **Windows**：xbintsc 自带 **MinGW-w64 ABI** 工具链（clang + lld + CRT + 导入库），
>   无需用户安装编译器。

> **状态 — P0 ✅ 已落地，P1 🔄 进行中。** `src/driver/toolchain-provider.ts`
> 按 `env → vendor → PATH` 解析工具链；`npm run runtime` 产出预编译库到
> `runtime/lib/<os>-<arch>/{core,ext_<name>}.a`，`build` 优先使用它们；
> `xbintsc doctor` 报告解析结果。`npm run fetch-toolchain` 把固定版本工具链
> （Linux 用 LLVM `18.1.8`，Windows 用 llvm-mingw `20260908`）下载到
> `vendor/<os>-<arch>/`，`resolveToolchain()` 优先使用它并以 `-fuse-ld=lld`
> 链接；CI `self-contained` 作业负责验证。macOS 走 Command Line Tools。

## 1. 目标与非目标

**目标**

- 干净机器（仅操作系统自带 libc）上，解压/安装 `xbintsc` 后即可 `build` + `run`。
- 三平台（Linux / macOS / Windows）一致。
- 保留现有 `emit`（只输出 LLVM IR 文本）的零依赖特性。

**非目标（本阶段）**

- 跨平台编译（cross-compile）：先只支持 host 平台。
- 彻底摆脱系统 libc：动态链接 libc 属于「操作系统自带」，不算「用户安装的工具」。
  （可选的 `-static` / musl 见 P5。）

## 2. 现状盘点

| 阶段 | 现在的实现 | 外部依赖 |
| --- | --- | --- |
| 前端 TS→IR | xbintsc 自己完成（已自举） | 无 |
| IR→目标文件 | `clang -c`（`src/driver/toolchain.ts` `compileIr`） | clang |
| C 运行期→目标文件 | 运行期用 `clang -c` 现编 22+ 个 `.c`（`src/driver/compiler.ts` `ensureRuntimeObjects`） | clang + C 头文件 |
| 链接 | `clang ... -o`（内部再调系统 `ld`/`ld64`/`link.exe`） | clang + 系统链接器 + libc/CRT |

结论：`build` / `run` 强依赖 clang；`emit` 已零依赖。参考 `doc/DESIGN.md` 的
「Replacing llc」一节，早期就是为了不装 `llc` 才改用 clang。

## 3. 方案对比与选型

| 方案 | 做法 | 体积 | 工作量 | 可靠性 | 评价 |
| --- | --- | --- | --- | --- | --- |
| A 系统工具链 | 维持现状，要求用户装 clang | 0 | 0 | 低 | 不满足目标 |
| **B 捆绑工具链（Zig 式）** | 随包分发 `clang`/`lld` + 预编译运行期 | 大 | 中 | 高 | ⭐ **推荐主干** |
| C libLLVM/liblld 进程内（Rust 式） | LLVM-C 出 `.o`，liblld 链接 | 中 | 大 | 中 | 长期优化 |
| D 自研后端 + 链接器 | 自己出机器码/目标文件 | 小 | 极大 | 低 | 不现实 |

**选型：B 为主干，预留 C。** B 改动最小、最快达成目标、可直接复用现有
self-host 产物；C 能进一步缩小体积，但与 TS/Node 栈结合成本高，
作为 P4 的可选演进。注意：`lld` **只是链接器**，无法替代 clang 的
「IR→obj」「C→obj」两个角色，因此任何方案都必须同时解决这两者。

## 4. 目标架构

```
xbintsc（自包含）
├── 编译器核心  TS → LLVM IR 文本           # 已有，且已自举
├── 运行期库    runtime/lib/<os>-<arch>/*.a(.lib)   # 新增：CI 预编译
├── 工具链抽象  src/driver/toolchain-provider.ts     # 新增：可插拔
│     ├── IR lowerer   IR → .o     （捆绑 llc/clang；长期 libLLVM）
│     ├── C compiler   .c → .o     （仅回退用；默认走预编译库）
│     └── linker       .o → 可执行 （捆绑 lld；长期 liblld）
└── vendor/<os>-<arch>/   clang/lld/CRT/导入库       # 新增：随包分发
```

工具链解析顺序（新增 `resolveToolchain()`）：

1. 环境变量覆盖：`xbintsc_TOOLCHAIN` / `xbintsc_CLANG` / `xbintsc_LLD`
2. 随包 `vendor/<os>-<arch>/`
3. 系统 `PATH`（开发友好，可关闭）

找不到时给出明确错误，并提示 `xbintsc doctor`。

## 5. 分阶段计划

### P0 工具链抽象 + 预编译运行期（地基，收益最大）— ✅ 已落地

- `src/driver/toolchain-provider.ts`：`resolveToolchain()` 按
  `env(xbintsc_CLANG / xbintsc_TOOLCHAIN) → vendor/ → PATH` 解析，返回驱动路径
  与额外链接参数。
- `scripts/build-runtime.ts`（`npm run runtime`）把 C 运行期编成静态库
  `runtime/lib/<os>-<arch>/core.a` 与 `ext_<name>.a`（Windows 在 MinGW 工具链落地前
  暂不生成）。
- `src/driver/compiler.ts` `ensureRuntimeObjects()` **优先使用已存在的库**
  （`BuildOptions.preferPrebuilt`，默认 true），缺失时回退现编 `.c`。
- `src/driver/paths.ts`：`platformSlug()` / `findVendorDir()` / `vendorRootDir()`；
  `src/driver/runtime-lib.ts`：`runtimeLibDir()` / `findRuntimeLibrary()`。
- `xbintsc doctor` 报告解析到的工具链与运行期库位置。
- 验收：已满足——`emit` 无需 clang；链接时优先用库，库缺失时自动回退到源码。

> 预编译运行期是所有方案的前提：它让运行期不再需要 C 头文件 / SDK，
> 并能显著缩短首次构建。

### P1 随包工具链 bundle — 🔄 进行中

- `src/driver/toolchain-download.ts` 固定每个 host 的 bundle；`npm run
  fetch-toolchain`（`scripts/fetch-toolchain.ts`）下载并解压到 `vendor/<os>-<arch>/`：
  - Linux：LLVM 官方 `18.1.8` release → `bin/clang`、`bin/ld.lld`、`bin/llvm-ar`。
  - Windows：llvm-mingw `20260908`（`ucrt-x86_64`）→ clang + lld + MinGW-w64
    的 sysroot / CRT / 导入库。
  - macOS：无（走 Command Line Tools）。
- `resolveToolchain()` 选中 `vendor/<os>-<arch>/bin/clang`，并默认加
  `-fuse-ld=lld` 使用自带链接器；可用 `xbintsc_LINKER_ARGS` 覆盖。
- Linux 官方构建仍链接已被移除的 `libtinfo.so.5` soname；fetch 脚本把它一并放入
  `vendor/<os>-<arch>/lib/`，`resolveToolchain()` 为工具链子进程设置
  `LD_LIBRARY_PATH` 指向该目录。
- CI `self-contained` 作业拉取 bundle、断言 `doctor` 在 Linux/Windows 报告
  `(vendor)`，再 `build` + `run` 一个程序。
- 验收：纯净 Linux 容器（无 clang/ld）、**装有** Command Line Tools 的 macOS、
  未装 Visual Studio 的 Windows 上，`build` + `run` 全部通过。

### P2 分发

- `release.yml`：发布包结构为
  `xbintsc-<os>-<arch>.tar.zst`，内含 `bin/xbintsc` + `vendor/` + `runtime/lib/`。
- npm：用 `optionalDependencies` 提供 per-platform 包（`@xbintsc/<platform>`），
  或 postinstall 下载（校验 sha256）。launcher（`bin/xbintsc.js`）负责定位 `vendor/`。
- ✅ `xbintsc doctor`：打印解析到的工具链来源、版本、路径与运行期库位置。

### P3 平台专项打磨

- **macOS**：直接使用 Command Line Tools 的系统工具链，不随包分发，因此不涉及
  quarantine / 签名问题；`ld64.lld`/SDK 由 CLT 提供（见第 6 节风险）。
- **Windows**：确定 ABI（推荐 MinGW-w64 自带 CRT/导入库以规避 MSVC SDK 依赖）；
  把 `-lws2_32` 等改为自带导入库。
- **Linux**：可选 `-static`（musl）。

### P4（可选，长期）libLLVM / liblld 进程内化

- 写一个 **Node-API 原生插件**封装 **LLVM-C**（`LLVMTargetMachineEmitToFile`），
  替代 `llc`/`clang` 产出 `.o`，缩小 bundle 体积。
- 进一步封装 `liblld`（无稳定 C API，需要 C++ 胶水）实现进程内链接，
  逼近「单文件 rustc 式」。验收：`vendor/` 仅保留 libLLVM/liblld。

### P5（可选）完全静态 / 无 libc

- Linux：自带 musl CRT/libc 支持 `-static`。
- 进一步 `-nostdlib` + 直接 syscall 运行时（工程量极大，独立课题）。

## 6. 平台专项风险

| 平台 | 风险 | 缓解 |
| --- | --- | --- |
| macOS | 下载的 bundle 带 quarantine，被 Gatekeeper 拦截；Apple Silicon 需签名 | 依赖 Command Line Tools，不再分发二进制，因此不涉及 quarantine |
| macOS | `ld64.lld` 需要 SDK 的 `libSystem.tbd`，SDK 来自 Command Line Tools | 把 Command Line Tools 作为已记录的前置要求（[`requirements.md`](./requirements.md)）；不再评估随包 `.tbd` |
| Linux | 官方 LLVM 构建链接已被移除的 `libtinfo.so.5` soname | 在 `vendor/lib` 自带 `libtinfo.so.5`，并为工具链子进程设置 `LD_LIBRARY_PATH` |
| Windows | MSVC 路线依赖 Windows SDK 导入库/CRT，用户未必安装 | 选 MinGW-w64 ABI，自带 CRT 与导入库（不依赖 MSVC SDK） |
| 通用 | 体积膨胀、npm 包大小限制 | 用 per-platform 包 / release 归档；压缩 `tar.zst` |
| 通用 | emit 的 IR 与 vendored LLVM 版本不匹配 | 固定 LLVM 版本，纳入版本常量与缓存键 |
| 通用 | 交叉编译矩阵爆炸 | 先只支持 host，明确声明 |

## 7. 总体验收标准

1. 干净机器（仅 OS 自带 libc；macOS 需按 `requirements.md` 装好 Command Line
   Tools）上：解压 release 包即可 `xbintsc build` / `run`。
2. CI 新增 **"no system toolchain" 作业**：把 `PATH` 里的 clang/ld/llc 全部屏蔽，
   或用最小容器，验证 `build` + `run` 仍成功。
3. `emit` 始终保持零外部依赖。
4. `xbintsc doctor` 能清晰报告工具链来源。

## 8. 里程碑（相对顺序，不承诺日期）

| 里程碑 | 内容 | 产出 |
| --- | --- | --- |
| M1 | P0 完成 | 预编译运行期库 + 工具链抽象 |
| M2 | P1 完成 | 三平台 bundle 工具链，纯净环境通过 |
| M3 | P2 完成 | release 归档 + npm 分发 + `doctor` |
| M4 | P3 完成 | macOS/Windows 打磨，全平台自包含 |
| M5 | P4/P5（可选） | libLLVM 化 / 完全静态 |

## 9. 关键决策

- **预编译运行期成库**是所有方案的前提。
- 工具链解析必须**可插拔、可覆盖**：默认 bundle，允许系统回退。
- 路线演进顺序：**先"能跑"（bundle clang+lld）→ 再"变小"（libLLVM+lld）→ 最后"变纯"（自研后端）**。
- `lld` 只解决「链接」这一环，方案里必须显式安排 IR→obj 与 C→obj 的归属。
