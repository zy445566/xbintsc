# xbintsc 使用前置要求

本文档说明一台机器要让 `xbintsc` **编译并链接出原生二进制**需要具备什么。
xbintsc 不再自带编译器，因此每个平台都需要安装一个兼容 clang 的工具链。
`xbintsc emit`（仅输出 LLVM IR 文本）在任何平台都没有要求。

## 总览

| 平台 | 前置要求 | 由谁提供 |
| --- | --- | --- |
| Linux | `clang` + `lld`（glibc 发行版） | 用户（包管理器） |
| macOS | **Xcode Command Line Tools** | 用户一次性安装 |
| Windows | **LLVM** + **Visual Studio C++ 生成工具**（MSVC ABI） | 用户一次性安装 |
| 任意 | `xbintsc emit` | 无需任何东西 |

Windows 发布包面向 **MSVC ABI**：clang 使用 Windows SDK
以及 MSVC 的 C/C++ 运行库与头文件。

## 预编译发布包（推荐）

从 [GitHub Releases](https://github.com/zy445566/xbintsc/releases/latest) 下载对应
平台的归档（每个都附带 `.sha256`）：

| 平台 | 归档 |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-<version>-win32-x64.tar.zst` / `xbintsc-<version>-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-<version>-linux-x64.tar.zst` / `xbintsc-<version>-linux-arm64.tar.zst` |
| macOS x64 / arm64（Apple Silicon） | `xbintsc-<version>-darwin-x64.tar.zst` / `xbintsc-<version>-darwin-arm64.tar.zst` |

（仅当构建机没有 `zstd` 时才会回退为 `.tar.gz`。）

`<version>` 是发布版本号（例如 `0.3.14`），会写入归档文件名，避免不同版本的下载文件相互覆盖。

每个归档解压出的结构一致：

```text
xbintsc-<os>-<arch>/
  bin/xbintsc[.exe]      编译器
  runtime/               C 运行期 + 预编译 runtime/lib/<os>-<arch>/
```

Windows 归档不含预编译 `runtime/lib/`；驱动会用用户的 clang 按需编译 C 运行期。

解压后把 `bin/` 加入 `PATH`（或直接调用 `bin/xbintsc`），再按下面的平台说明安装
工具链。建议先校验下载内容：

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

## Linux —— 安装 clang 与 lld

常规的 glibc 发行版加上 `clang`、`lld` 即可：

```sh
sudo apt-get install -y clang lld        # Debian / Ubuntu
sudo dnf install -y clang lld            # Fedora / RHEL
sudo pacman -S --needed clang lld        # Arch
sudo zypper install -y clang lld         # openSUSE
sudo apk add clang lld                   # Alpine
```

用 `clang --version` 验证。如果 xbintsc 用错了链接器，可用 `xbintsc_CLANG` 指定
clang，并通过 `xbintsc_LINKER_ARGS` 追加参数（例如 `-fuse-ld=lld`）。

## Windows —— 安装 LLVM 与 MSVC C++ 生成工具

用 `winget` 安装 LLVM 和 Visual Studio C++ 生成工具：

```powershell
winget install -e --id LLVM.LLVM
winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

clang 需要 MSVC/SDK 的环境变量（`INCLUDE`、`LIB`、`PATH`）才能找到 C 标准库头文件、
导入库和链接器。请在 **x64 Native Tools Command Prompt for VS 2022**（Windows on
ARM 上用 **ARM64 Native Tools** 提示符）中运行 `xbintsc`，或自行导入环境：

```powershell
& "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
```

用 `clang --version` 验证。（也可用 chocolatey：`choco install llvm`。）

## 从源码构建（仅贡献者需要）

- Node.js ≥ 22 仅在运行/构建编译器本身时需要；发布的独立二进制**不需要** Node。
- 重建运行期（`npm run runtime`）需要 C 编译器。
- `npm run package-release` 会在 `dist/release/` 下组装出各平台发布归档
  （`xbintsc-<os>-<arch>.tar.{gz,zst}` + `.sha256`）。

## 检查环境

```sh
xbintsc doctor
```

`doctor` 会报告解析到的工具链（env / 系统）、其版本与位置，以及运行期库目录。
