# obs-nv-boost

---

## 中文

### 关于本项目

本项目是 OBS Studio 的 NVIDIA NVENC 编码器插件（`obs-nvboost`），基于 OBS Studio 内置的 NVENC 插件（obs-nvenc，GPLv2+）修改而来，使用 [FFmpeg/nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) 头文件对接 NVIDIA Video Codec API，支持 D3D11 纹理直通编码（`_tex`）与 CUDA 软路径（`_soft`）两种接入方式。主要方向：

- **按 SDK 版本编译**：每个 NVENC SDK 版本编译出对应的插件 DLL，匹配相应最低驱动版本，让老显卡/老驱动也能用上新版编码器特性，新版驱动也能回退到老接口
- **更丰富的编码设置**：通过 opts-parser 透传自定义编码参数（`opts` 自定义选项）

### SDK 版本矩阵（为什么每个驱动对应一个 DLL）

NVENC 的 ABI 与 Video Codec SDK 版本绑定：插件编译时使用的 SDK 头文件版本必须不超过运行机器驱动所支持的 API 版本。本项目按下列矩阵各编译一个 DLL，按需选用：

| SDK 版本 | Windows 最低驱动 | 支持功能                             |
| -------- |  ---------------- | ------------------------------------ |
| 8.2      | 397.93           | 无 tuning / multi-pass / AV1         |
| 9.1      | 436.15           | 无 AV1                               |
| 10.0     | 450.51           | 引入 tuning、P1-P7 preset            |
| 11.1     | 471.41           | 引入`cb/crQPIndexOffset`             |
| 12.2     | 551.76           | 引入 AV1（12.0 起）、temporal filter |
| 13.1     | 610.0            | 最新 SDK                             |

### 下载

前往本仓库的 Releases 页面下载最新稳定发布版，或前往 Actions 页面下载最新构建。每个 SDK 版本对应一个独立的包（`obs-nvboost-sdk<版本>-windows-x64`）。

### 安装方法

按照自己的显卡驱动版本，选取对应 `sdk-<版本>` 目录，将包内的 `data`、`obs-plugins`（以及 `bin`）文件夹解压到 OBS Studio 主目录下，也就是看得见 `bin`、`data`、`obs-plugins` 这三个文件夹的目录下。

> 同一个 OBS 安装内只能安装一个版本的 `obs-nvboost.dll`。

### 本地编译（Windows）

前置条件：Windows、Git、Visual Studio Build Tools（含 CMake，脚本会从 VS 安装目录自动定位，找不到再回退 PATH）、OBS Studio 源码树（推荐 32.2.0，含子模块）。脚本默认使用 `H:\GitHub\obs-studio`，如路径不同请给所有脚本传 `-ObsPath <路径>`。

1. **一键矩阵构建 + 打包**（6 个 SDK 版本全部构建，产物进 `release\`）：
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Release.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Release.ps1 -SdkVersions 12.2,13.1   # 只构建指定版本
   ```
   
   脚本对每个 SDK 版本执行：检出对应 nv-codec-headers tag 并提取头文件到 `_deps\ffnvcodec-<tag>\include` → 幂等挂载插件到 OBS 源码树 → 以 `-DFFnvcodec_INCLUDE_DIR=<注入路径>` 重新 configure → 干净重编 `obs-nvboost` 与 `obs-nvboost-test` → 打包到 `release\sdk-<版本>\`，目录布局与插件 zip 一致（`obs-plugins\64bit\obs-nvboost.dll`、`bin\64bit\obs-nvboost-test.exe`、`data\obs-plugins\obs-nvboost\locale\`）。
2. **只构建单个 SDK 版本**（产物留在 OBS 构建树内，不打包）：
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build.ps1 -SdkVersion 12.2
   ```
3. **只做编译验证**（不打包，日志与结果摘要写入 `_buildtest\`）：
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-SdkMatrix.ps1
   ```

> - 首次 configure 时 CMake 会自动下载 OBS 预编译依赖（obs-deps 等）；`ENABLE_BROWSER=OFF` 跳过 CEF 下载。
> - configure 时会传 `-DENABLE_NVENC=OFF` 禁用 OBS 自带的 obs-nvenc：其强制要求 SDK 12，会阻塞低版本矩阵编译。
> - 切换 SDK 头文件后必须干净重编插件（MSBuild 增量判定不追踪 include 目录变化），脚本已自动处理。
> - OBS 依赖包自带一份 ffnvcodec 头文件，会遮蔽注入的 SDK 头；CMake 已用 `target_include_directories(... BEFORE)` 保证注入头优先，见 `obs-nvboost\CMakeLists.txt` 内注释。

### GitHub Actions 自动构建

`.github/workflows/build-sdk-matrix.yml` 在 push / PR / tag 时按上述 SDK 矩阵构建，并为每个 SDK 版本上传独立的构建产物。

---

## English

### About This Project

This is an NVIDIA NVENC encoder plugin for OBS Studio (`obs-nvboost`), derived from OBS Studio's built-in NVENC plugin (obs-nvenc, GPLv2+). It uses the [FFmpeg/nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) headers to interface with the NVIDIA Video Codec API, and supports both D3D11 texture pass-through encoding (`_tex`) and the CUDA software path (`_soft`). Main directions:

- **Per-SDK-version builds**: Each NVENC SDK version is built into its own plugin DLL matched to the corresponding minimum driver, so older GPUs/older drivers can also use the newer encoder features, while newer drivers can fall back to the older interfaces.
- **Richer encoding settings**: Custom encoding parameters are passed through via the opts-parser (the `opts` custom option).

### SDK Version Matrix (why one DLL per driver)

The NVENC ABI is tied to the Video Codec SDK version: the SDK headers a plugin is compiled with must not exceed the API version supported by the driver on the target machine. This project builds one DLL per entry of the following matrix; pick as needed:

| SDK Version | Minimum Windows driver | Supported features                           |
| ----------- | ---------------------- | -------------------------------------------- |
| 8.2         | 397.93                 | no tuning / multi-pass / AV1                 |
| 9.1         | 436.15                 | no AV1                                       |
| 10.0        | 450.51                 | introduces tuning, P1-P7 presets             |
| 11.1        | 471.41                 | introduces `cb/crQPIndexOffset`              |
| 12.2        | 551.76                 | introduces AV1 (since 12.0), temporal filter |
| 13.1        | 610.0                  | latest SDK                                   |

### Download

Go to the Releases page of this repository for the latest stable release, or go to the Actions page for the latest builds. Each SDK version has its own package (`obs-nvboost-sdk<version>-windows-x64`).

### Installation

Pick the `sdk-<version>` folder matching your graphics driver, then extract the `data`, `obs-plugins` (and `bin`) folders into the OBS Studio main directory, i.e., the folder where you can see the `bin`, `data`, and `obs-plugins` folders.

> Only one version of `obs-nvboost.dll` may be installed per OBS installation.

### Local Build (Windows)

Prerequisites: Windows, Git, Visual Studio Build Tools (includes CMake; the scripts locate it automatically from the Visual Studio installation directory, falling back to PATH if not found), and an OBS Studio source tree (32.2.0 recommended, with submodules). Scripts default to `H:\GitHub\obs-studio`; pass `-ObsPath <path>` to all scripts if yours differs.

1. **One-shot matrix build + packaging** (all 6 SDK versions, artifacts land in `release\`):
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Release.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Release.ps1 -SdkVersions 12.2,13.1   # build only the specified versions
   ```
   
   For each SDK version the script: checks out the matching nv-codec-headers tag and extracts the headers to `_deps\ffnvcodec-<tag>\include` → mounts the plugin into the OBS tree (idempotent) → re-configures with `-DFFnvcodec_INCLUDE_DIR=<injected path>` → clean-rebuilds `obs-nvboost` and `obs-nvboost-test` → packages into `release\sdk-<version>\` with the same layout as the plugin zip (`obs-plugins\64bit\obs-nvboost.dll`, `bin\64bit\obs-nvboost-test.exe`, `data\obs-plugins\obs-nvboost\locale\`).
2. **Single-SDK build** (output stays inside the OBS build tree, no packaging):
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build.ps1 -SdkVersion 12.2
   ```
3. **Compile-only matrix check** (no packaging; logs and summary go to `_buildtest\`):
   
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-SdkMatrix.ps1
   ```

> - The first configure downloads the OBS prebuilt dependencies (obs-deps etc.); `ENABLE_BROWSER=OFF` skips the CEF download.
> - Configure passes `-DENABLE_NVENC=OFF` to disable OBS's built-in obs-nvenc: it requires SDK 12 and would block the low-version matrix builds.
> - Switching SDK headers requires a clean rebuild of the plugin (MSBuild incremental builds do not track include-directory changes); the scripts handle this automatically.
> - The OBS dependency bundle ships its own ffnvcodec headers which would shadow the injected ones; CMake keeps the injected headers first via `target_include_directories(... BEFORE)`, see the comment in `obs-nvboost\CMakeLists.txt`.

### GitHub Actions Automated Builds

`.github/workflows/build-sdk-matrix.yml` builds the SDK matrix above on push / PR / tags and uploads separate artifacts per SDK version.

