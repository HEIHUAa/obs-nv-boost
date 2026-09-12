# Build-Release.ps1 - 按 NVENC SDK 版本矩阵批量构建并打包插件
#
# 用法:
#   .\scripts\Build-Release.ps1                                    # 默认矩阵 8.2/9.1/10.0/11.1/12.2/13.1
#   .\scripts\Build-Release.ps1 -SdkVersions 12.2,13.1            # 只构建指定版本
#
# 流程(每个 SDK 版本): 准备头文件 -> 干净重编插件 -> 复制产物到 release/sdk-<ver>/
# 打包布局与 obs-qsv-onevpl-boost 的 release/<variant>/ 一致:
#   release/sdk-<ver>/obs-plugins/64bit/obs-nvboost.dll
#   release/sdk-<ver>/bin/64bit/obs-nvboost-test.exe
#   release/sdk-<ver>/data/obs-plugins/obs-nvboost/locale/*.ini
param(
    [string[]]$SdkVersions = @("8.2", "9.1", "10.0", "11.1", "12.2", "13.1"),
    [string]$ObsPath = "H:\GitHub\obs-studio",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$OutRoot = Join-Path $ProjectRoot "release"
$CMake = "H:\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) { $CMake = "cmake" }

$PluginBuildDir = Join-Path $ObsPath "build_x64\plugins\obs-nvboost-build"
$ModuleName = "obs-nvboost"

# 0. 挂载插件到 OBS 树（幂等；未挂载过时补上）
$pluginsCmake = Join-Path $ObsPath "plugins\CMakeLists.txt"
if (-not (Test-Path $pluginsCmake)) { throw "OBS tree not found: $ObsPath (use -ObsPath)" }
$mountLine = "add_subdirectory($($ProjectRoot -replace '\\', '/')/obs-nvboost obs-nvboost-build)"
if (-not (Select-String -Path $pluginsCmake -Pattern $mountLine -SimpleMatch -Quiet)) {
    Add-Content -Path $pluginsCmake -Value "`r`n# obs-nv-boost: 树外开发目录（$ProjectRoot）`r`n$mountLine"
    Write-Host "==> Mounted plugin into $pluginsCmake"
}

$summary = @()
foreach ($sdk in $SdkVersions) {
    Write-Host "`n===== SDK $sdk ====="

    # 1. 准备头文件并捕获本次输出的确切 include 路径（不能 glob 取"最后"：
    #    字母序 n9.x > n13.x 会误选低版本头）
    $includeDir = & "$PSScriptRoot\Setup-FFnvcodec.ps1" -SdkVersion $sdk | Select-Object -Last 1
    Write-Host "==> Include dir: $includeDir"

    # 2. 强制干净重编插件：MSBuild 增量判定不追踪 include dir 变化，会产生 stale object
    Remove-Item -Recurse -Force $PluginBuildDir -ErrorAction SilentlyContinue

    # 3. configure（ENABLE_NVENC=OFF 禁用官方 obs-nvenc：其强制要求 SDK 12，会阻塞低版本
    #    SDK。本项目编码器 ID 已带 boost 段，与官方实现共存，无 ID 冲突）
    # PS5.1 下 2>&1 会把 stderr 包装成 ErrorRecord，Stop 偏好会因 CMake 警告误终止，
    # 此处局部降级为 Continue，成败由 $LASTEXITCODE 判定
    $ErrorActionPreference = "Continue"
    Set-Location $ObsPath
    & $CMake --preset windows-x64 -DENABLE_BROWSER=OFF -DENABLE_NVENC=OFF -DFFnvcodec_INCLUDE_DIR="$includeDir" 2>&1 |
        Out-File (Join-Path $ProjectRoot "_buildtest\release-sdk-$sdk-configure.log") -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "SDK $sdk configure FAILED"
        $summary += [pscustomobject]@{ SDK = $sdk; Result = "FAIL(configure)" }
        continue
    }

    # 4. build
    & $CMake --build build_x64 --config $Config --target $ModuleName "$ModuleName-test" 2>&1 |
        Out-File (Join-Path $ProjectRoot "_buildtest\release-sdk-$sdk-build.log") -Encoding utf8
    $ErrorActionPreference = "Stop"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "SDK $sdk build FAILED"
        $summary += [pscustomobject]@{ SDK = $sdk; Result = "FAIL(build)" }
        continue
    }

    # 5. 打包到 release/sdk-<ver>/（布局同 obs-qsv-onevpl-boost 的 release/<variant>/）
    $dest = Join-Path $OutRoot "sdk-$sdk"
    New-Item -ItemType Directory -Force -Path (Join-Path $dest "obs-plugins\64bit") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dest "bin\64bit") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dest "data\obs-plugins\$ModuleName\locale") | Out-Null
    Copy-Item (Join-Path $PluginBuildDir "$Config\$ModuleName.dll") (Join-Path $dest "obs-plugins\64bit\") -Force
    Copy-Item (Join-Path $PluginBuildDir "$Config\$ModuleName.pdb") (Join-Path $dest "obs-plugins\64bit\") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $PluginBuildDir "$Config\$ModuleName-test.exe") (Join-Path $dest "bin\64bit\") -Force
    Copy-Item (Join-Path $ProjectRoot "obs-nvboost\data\locale\*.ini") (Join-Path $dest "data\obs-plugins\$ModuleName\locale") -Force
    Write-Host "==> Packaged: $dest"
    $summary += [pscustomobject]@{ SDK = $sdk; Result = "OK" }
}

Write-Host "`n===== SUMMARY ====="
$summary | Format-Table -AutoSize
$summary | Export-Csv (Join-Path $OutRoot "build-summary.csv") -NoTypeInformation
