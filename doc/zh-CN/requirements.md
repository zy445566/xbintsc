# xbintsc 使用前置要求

本文档说明一台机器要让 `xbintsc` **编译并链接出原生二进制**需要具备什么。
xbintsc 不再自带编译器，因此每个平台都需要安装一个兼容 clang 的工具链。
`xbintsc emit`（仅输出 LLVM IR 文本）在任何平台都没有要求。

## 最低版本

| 要求 | 最低版本 | 说明 |
| --- | --- | --- |
| clang / LLVM | **16** | 生成的 IR 使用不透明指针，而 LLVM 16 起不透明指针是唯一的指针表示。 |
| Node.js | **22** | 仅用于从源码运行或构建编译器；发布二进制是独立的，不需要 Node。 |

clang 15 及更旧版本会在任何程序的第一个字符串字面量处失败：

```
build/app.ll:382:37: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'
  %r0 = call i64 @xt_string_new(i8* @.str.0, i64 5)
```

有类型指针在 LLVM 16 中被移除（[发布说明](https://github.com/llvm/llvm-project/blob/release/15.x/llvm/docs/ReleaseNotes.rst#changes-to-the-llvm-ir)），
所以 xbintsc 现在**在编译任何东西之前先检查版本**：

- `xbintsc build` / `run` 一旦发现解析到的驱动低于 16 就立即停止，并给出可操作的错误，
  而不是生成无法编译的 IR。
- `xbintsc doctor` 会打印 `status : ok` 或 `status : too old`，在工具链无法构建二进制时
  以非零码退出，脚本和 CI 可以把它当作预检。
- 搜索时会优先选择 16+ 的 clang：Windows 上即使 `PATH` 里靠前的是旧 clang，
  也能找到 Visual Studio 自带的 LLVM
  （`<VS 安装目录>\VC\Tools\Llvm\x64\bin\clang.exe`）。可以用 `xbintsc_CLANG` 覆盖；
  显式指定的旧版本会得到 "too old" 错误，而不是被静默忽略。

把 IR 拿到别处编译来绕过该限制是不受支持的。

## 总览

| 平台 | 前置要求 | 由谁提供 |
| --- | --- | --- |
| Linux | `clang` + `lld`（glibc 发行版） | 用户（包管理器） |
| macOS | **Xcode Command Line Tools** | 用户一次性安装 |
| Windows | **LLVM** + **Visual Studio C++ 生成工具**（MSVC ABI） | 用户一次性安装 |
| 任意 | `xbintsc emit` | 无需任何东西 |

Windows 发布包面向 **MSVC ABI**：clang 使用 Windows SDK
以及 MSVC 的 C/C++ 运行库与头文件。

可选优化特性额外依赖两个工具：

- `--lto` 需要 **lld**（`-fuse-ld=lld`）；上面的 LLVM 安装包已包含。
- `--pgo-use` 需要 **llvm-profdata** 合并 `.profraw`。驱动优先使用 clang
  旁边的副本，可用 `xbintsc_LLVM_PROFDATA` 覆盖。

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

## Windows —— 安装 MSVC C++ 生成工具（与 LLVM）

Windows 上的 xbintsc 面向 **MSVC ABI**。用 `winget` 安装 Visual Studio C++
生成工具和 LLVM：

```powershell
winget install -e --id LLVM.LLVM
winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Visual Studio 2022 自带的 LLVM（`VC\Tools\Llvm\x64\bin\clang.exe`，当前版本
19.1.5）已经够用，独立安装的 LLVM 也可以。但编译器必须以 MSVC ABI 为目标
（`Target: *-windows-msvc`）：面向其他 Windows ABI 的 clang 无法与 MSVC
运行库链接，`xbintsc` 会直接给出明确错误并拒绝使用。

通常**不再需要**开发者命令提示符：`xbintsc` 会按绝对路径找到 Visual Studio
自带的 LLVM，而这个 clang 会自行定位 MSVC/SDK 的头文件、库与链接器。如果你的
安装仍需要显式导入环境，可在 **x64 Native Tools Command Prompt for VS 2022**
（Windows on ARM 上用 **ARM64 Native Tools**）中构建，或手动导入：

```powershell
& "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
```

用 `xbintsc doctor`（或 `clang --version`）验证；输出会列出解析到的编译器、
版本与目标。

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
