# xbintsc 使用前置要求

本文档说明一台机器要让 `xbintsc` **编译并链接出原生二进制**需要具备什么。
只有工具链是与平台相关的；`xbintsc emit`（仅输出 LLVM IR 文本）在任何平台都
没有要求。

## 总览

| 平台 | 前置要求 | 由谁提供 |
| --- | --- | --- |
| Linux | 系统 C 库（glibc） | 操作系统自带 |
| macOS | **Xcode Command Line Tools** | 用户一次性安装 |
| Windows | 除操作系统外无需额外安装 | xbintsc 自带 MinGW-w64 ABI 工具链 |
| 任意 | `xbintsc emit` | 无需任何东西 |

## 预编译发布包（推荐）

从 [GitHub Releases](https://github.com/zy445566/xbintsc/releases/latest) 下载对应
平台的自包含归档（每个都附带 `.sha256`）：

| 平台 | 归档 |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-<version>-win32-x64.tar.zst` / `xbintsc-<version>-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-<version>-linux-x64.tar.zst` / `xbintsc-<version>-linux-arm64.tar.zst` |
| macOS x64 / arm64（Apple Silicon） | `xbintsc-<version>-darwin-x64.tar.zst` / `xbintsc-<version>-darwin-arm64.tar.zst` |

（仅当构建机没有 `zstd` 时才会回退为 `.tar.gz`。）

`<version>` 是发布版本号（例如 `0.3.8`），会写入归档文件名，避免不同版本的下载文件相互覆盖。

每个归档解压出的结构一致：

```text
xbintsc-<os>-<arch>/
  bin/xbintsc[.exe]      编译器
  runtime/               C 运行期 + 预编译 runtime/lib/<os>-<arch>/
  vendor/<os>-<arch>/    随包工具链（Linux/Windows）
```

解压后把 `bin/` 加入 `PATH`（或直接调用 `bin/xbintsc`）即可，无需安装步骤。
建议先校验下载内容：

```sh
sha256sum -c xbintsc-<version>-linux-x64.tar.zst.sha256   # macOS 用：shasum -a 256 -c
```

## macOS —— 安装 Xcode Command Line Tools

xbintsc 使用 Command Line Tools 自带的链接器与系统 SDK（`libSystem` 等）来产出
可执行文件。请先安装一次：

```sh
xcode-select --install
```

验证：

```sh
xcode-select -p            # 打印当前开发者目录
xcrun --show-sdk-path      # 打印 SDK 路径
```

若未安装，`xbintsc build` 会给出明确报错并指向本文档；`xbintsc emit` 仍可正常使用。

## Linux

常规的 glibc 发行版即可，无需安装任何东西。发布包自带 clang + lld 以及它们所需的
共享库（尤其是旧 soname `libtinfo.so.5`），因此在没有编译器的系统上也能直接使用。
将来可选的静态二进制会使用自带的 musl CRT。

## Windows

无需安装任何东西。xbintsc 发布包中自带 **MinGW-w64** ABI 工具链（clang + lld +
CRT + 导入库），因此在干净的 Windows 上也能直接 `xbintsc build`。

## 从源码构建（仅贡献者需要）

- Node.js ≥ 22 仅在运行/构建编译器本身时需要；发布的独立二进制**不需要** Node。
- C 编译器仅在重建运行期（`npm run runtime`）时需要，**使用**发布版 xbintsc 时不需要。
- `npm run fetch-toolchain` 会下载 xbintsc 随包分发的工具链并解压到
  `vendor/<os>-<arch>/`；之后 `resolveToolchain()` 会优先于 `PATH` 使用它
  （Linux 用 LLVM，Windows 用 llvm-mingw）。macOS 上此命令为空操作——直接使用
  Command Line Tools。可用 `xbintsc_LINKER_ARGS` / `xbintsc_CLANG` 覆盖解析结果。
- `npm run package-release` 会在 `dist/release/` 下组装出各平台发布归档
  （`xbintsc-<os>-<arch>.tar.{gz,zst}` + `.sha256`）。

## 检查环境

```sh
xbintsc doctor
```

`doctor` 会报告解析到的工具链（env / 自带 / 系统）、其版本与位置，以及运行期库目录。
