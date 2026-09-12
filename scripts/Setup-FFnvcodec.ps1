# Setup-FFnvcodec.ps1 - 准备指定版本的 NVENC SDK 头文件
#
# 用法:
#   .\scripts\Setup-FFnvcodec.ps1 -SdkVersion 12.2     # SDK 12.2 系列最新 tag
#   .\scripts\Setup-FFnvcodec.ps1 -SdkVersion latest   # 全部 tag 中最新
#
# 流程: 在 third-party/nv-codec-headers 中 checkout 匹配的最新 tag，
#       提取 include/ffnvcodec 到 _deps/ffnvcodec-<tag>/include/，
#       CMake 侧通过 -DFFnvcodec_INCLUDE_DIR=<输出>/include 注入。
param(
    [Parameter(Mandatory = $true)]
    [string]$SdkVersion,
    [string]$RepoDir,
    [string]$OutRoot
)

$ErrorActionPreference = "Stop"

# param 默认值求值阶段 $PSScriptRoot 不可靠，在执行体内解析
if (-not $RepoDir) { $RepoDir = Join-Path $PSScriptRoot "..\third-party\nv-codec-headers" }
if (-not $OutRoot) { $OutRoot = Join-Path $PSScriptRoot "..\_deps" }

# 本地无仓库时自动补齐（完整 clone，保留全部 tag 以支持按 SDK 切换）
if (-not (Test-Path (Join-Path $RepoDir ".git"))) {
    Write-Host "==> Cloning nv-codec-headers into $RepoDir"
    git clone https://github.com/FFmpeg/nv-codec-headers.git $RepoDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

# 确定目标 tag：匹配 n<SdkVersion>.x 系列，按版本号取最大
if ($SdkVersion -eq "latest") {
    $tag = git -C $RepoDir tag --sort=-v:refname | Select-Object -First 1
} else {
    $tags = git -C $RepoDir tag -l "n$SdkVersion.*" | ForEach-Object {
        [pscustomobject]@{ Tag = $_; Ver = [version]$_.TrimStart("n") }
    } | Sort-Object Ver -Descending
    if (-not $tags) { throw "No nv-codec-headers tag matches SDK '$SdkVersion'" }
    $tag = $tags[0].Tag
}
if (-not $tag) { throw "Failed to resolve a tag for SDK '$SdkVersion'" }

Write-Host "==> Checking out $tag"
git -C $RepoDir checkout $tag --force --quiet
if ($LASTEXITCODE -ne 0) { throw "git checkout $tag failed" }

# 提取头文件到独立目录，使 CMake 注入路径不受 git 工作区当前 checkout 状态影响
$outDir = Join-Path $OutRoot "ffnvcodec-$tag"
New-Item -ItemType Directory -Force -Path (Join-Path $outDir "include") | Out-Null
Copy-Item (Join-Path $RepoDir "include\ffnvcodec") (Join-Path $outDir "include\ffnvcodec") -Recurse -Force

$major = (git -C $RepoDir describe --tags --exact-match) -replace "^n", ""
Write-Host "==> SDK headers ready: $((Resolve-Path $outDir).Path)\include"
# 输出 include 路径供调用方捕获（Write-Host 不会混入管道）
Write-Output "$((Resolve-Path $outDir).Path)\include"
