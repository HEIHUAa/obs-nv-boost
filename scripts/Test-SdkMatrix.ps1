# Test-SdkMatrix.ps1 - 批量测试多个 NVENC SDK 版本的编译
# 输出: _buildtest/sdk-<ver>.log 与结果摘要
param(
    [string[]]$SdkVersions = @("13.1", "13.0", "12.1", "12.0"),
    [string]$ObsPath = "H:\GitHub\obs-studio"
)

$ErrorActionPreference = "Continue"
$ProjectRoot = Join-Path $PSScriptRoot ".."
$OutDir = Join-Path $ProjectRoot "_buildtest"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$CMake = "H:\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) { $CMake = "cmake" }

$summary = @()
foreach ($sdk in $SdkVersions) {
    Write-Host "`n===== SDK $sdk ====="
    # 捕获本次 Setup 输出的确切 include 路径（不能 glob 取"最后"：
    # 字母序 n9.x > n13.x，会导致低版本测试误用高版本头产生假 PASS）
    $includeDir = & "$PSScriptRoot\Setup-FFnvcodec.ps1" -SdkVersion $sdk | Select-Object -Last 1

    $confLog = Join-Path $OutDir "sdk-$sdk-configure.log"
    Set-Location $ObsPath

    # 强制干净重编：切换 include dir 后 MSBuild 增量判定不会重编插件（新路径头文件
    # 不在旧 .obj 追踪表里），会产生 stale object 假阳性，必须删除插件构建目录
    Remove-Item -Recurse -Force (Join-Path $ObsPath "build_x64\plugins\obs-nvboost-build") -ErrorAction SilentlyContinue

    # ENABLE_NVENC=OFF: 禁用 OBS 自带 obs-nvenc（其 find_package 强制 SDK 12，
    # 会阻塞低版本矩阵；本项目编码器 ID 已带 boost 段，与官方实现共存，无 ID 冲突）
    & $CMake --preset windows-x64 -DENABLE_BROWSER=OFF -DENABLE_NVENC=OFF -DFFnvcodec_INCLUDE_DIR="$includeDir" 2>&1 |
        Out-File $confLog -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        $summary += [pscustomobject]@{ SDK = $sdk; Stage = "configure"; Result = "FAIL" }
        continue
    }

    $buildLog = Join-Path $OutDir "sdk-$sdk-build.log"
    & $CMake --build build_x64 --config Release --target obs-nvboost obs-nvboost-test 2>&1 |
        Out-File $buildLog -Encoding utf8
    if ($LASTEXITCODE -eq 0) {
        $summary += [pscustomobject]@{ SDK = $sdk; Stage = "build"; Result = "PASS" }
    } else {
        $summary += [pscustomobject]@{ SDK = $sdk; Stage = "build"; Result = "FAIL" }
    }
}

Write-Host "`n===== SUMMARY ====="
$summary | Format-Table -AutoSize
$summary | Export-Csv (Join-Path $OutDir "summary.csv") -NoTypeInformation
