﻿# Build.ps1 - 本地一键构建入口
#
# 用法:
#   .\scripts\Build.ps1 -SdkVersion 12.2                          # 默认 OBS 树 H:\GitHub\obs-studio
#   .\scripts\Build.ps1 -SdkVersion 13.1 -ObsPath D:\obs-studio   # 指定 OBS 源码树
#
# 流程: 准备 SDK 头文件 -> 挂载插件到 OBS 树 -> configure(注入 FFnvcodec_INCLUDE_DIR)
#       -> 构建 obs-nvboost 与 obs-nvboost-test
param(
    [Parameter(Mandatory = $true)]
    [string]$SdkVersion,
    [string]$ObsPath = "H:\GitHub\obs-studio",
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot + "\.."
$CMake = "H:\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) { $CMake = "cmake" } # 回退到 PATH 中的 cmake

# 1. 准备目标 SDK 头文件（捕获本次输出的确切 include 路径，
#    不能 glob 取"最后"：字母序 n9.x > n13.x 会误选低版本头）
& "$PSScriptRoot\Setup-FFnvcodec.ps1" -SdkVersion $SdkVersion
$includeDir = & "$PSScriptRoot\Setup-FFnvcodec.ps1" -SdkVersion $SdkVersion | Select-Object -Last 1

# 2. 挂载插件到 OBS 树（幂等）
$pluginsCmake = Join-Path $ObsPath "plugins\CMakeLists.txt"
$mountLine = 'add_subdirectory(H:/GitHub/obs-nv-boost/obs-nvboost obs-nvboost-build)'
if (-not (Select-String -Path $pluginsCmake -Pattern $mountLine -SimpleMatch -Quiet)) {
    Add-Content -Path $pluginsCmake -Value "`r`n# obs-nv-boost: 树外开发目录（H:\GitHub\obs-nv-boost）`r`n$mountLine"
    Write-Host "==> Mounted plugin into $pluginsCmake"
}

# 3. configure（SDK 头文件路径注入；ENABLE_NVENC=OFF 禁用官方 obs-nvenc：其强制要求 SDK 12，
#    会阻塞低版本 SDK。本项目编码器 ID 已带 boost 段，与官方实现共存，无 ID 冲突）
# 切换 SDK 后必须强制干净重编插件：MSBuild 增量判定不会因 include dir 变化重编，会产生 stale object
Remove-Item -Recurse -Force (Join-Path $ObsPath "build_x64\plugins\obs-nvboost-build") -ErrorAction SilentlyContinue
Set-Location $ObsPath
& $CMake --preset windows-x64 -DENABLE_BROWSER=OFF -DENABLE_NVENC=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF -DFFnvcodec_INCLUDE_DIR="$includeDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# 4. build
& $CMake --build build_x64 --config $Config --target obs-nvboost obs-nvboost-test
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "==> Done. Artifacts:"
Get-ChildItem (Join-Path $ObsPath "build_x64\plugins\obs-nvboost-build\$Config") | Select-Object Name
