[CmdletBinding()]
param(
    [switch]$AutoInstall,
    [switch]$NoPrompt,
    [switch]$SkipVcpkg,
    [switch]$RunBuildCheck,
    [string]$Triplet = "x64-windows",
    [string]$VcpkgRoot
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"

$script:MissingItems = [System.Collections.Generic.List[string]]::new()
$script:Warnings = [System.Collections.Generic.List[string]]::new()
$script:Actions = [System.Collections.Generic.List[string]]::new()

function Write-Section([string]$Text) {
    Write-Host ""
    Write-Host "== $Text ==" -ForegroundColor Cyan
}

function Write-Ok([string]$Text) {
    Write-Host "[OK] $Text" -ForegroundColor Green
}

function Write-WarnLine([string]$Text) {
    Write-Host "[WARN] $Text" -ForegroundColor Yellow
}

function Write-Info([string]$Text) {
    Write-Host "[INFO] $Text" -ForegroundColor Gray
}

function Test-CommandExists([string]$Name) {
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-CommandVersionText([string]$Name) {
    try {
        $cmd = Get-Command $Name -ErrorAction Stop
        if ($cmd.Version) {
            return $cmd.Version.ToString()
        }
    } catch {
    }

    try {
        $line = (& $Name --version 2>$null | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            return $line.Trim()
        }
    } catch {
    }

    return "unknown"
}

function Get-VsWherePath {
    if ($env:ProgramFiles -and $env:ProgramFiles -ne "") {
        $candidate = Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    if ($env:"ProgramFiles(x86)" -and $env:"ProgramFiles(x86)" -ne "") {
        $candidate = Join-Path $env:"ProgramFiles(x86)" "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Test-MsvcInstalled {
    if (Test-CommandExists "cl.exe") {
        return $true
    }

    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) {
        return $false
    }

    $installPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    return -not [string]::IsNullOrWhiteSpace(($installPath | Out-String).Trim())
}

function Get-VisualStudioInstallPath {
    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) {
        return $null
    }

    $installPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    $trimmed = ($installPath | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        return $null
    }
    return $trimmed
}

function Confirm-Install([string]$DisplayName) {
    if ($AutoInstall) {
        return $true
    }
    if ($NoPrompt) {
        return $false
    }
    $answer = Read-Host "缺少 $DisplayName，是否现在尝试自动安装？[y/N]"
    return $answer -match '^[Yy]$'
}

function Try-InstallWithWinget([string]$PackageId, [string]$DisplayName, [string]$OverrideArgs) {
    if (-not (Test-CommandExists "winget")) {
        return $false
    }

    $args = @(
        "install",
        "--id", $PackageId,
        "--exact",
        "--accept-package-agreements",
        "--accept-source-agreements"
    )
    if (-not [string]::IsNullOrWhiteSpace($OverrideArgs)) {
        $args += @("--override", $OverrideArgs)
    }

    Write-Info "执行: winget $($args -join ' ')"
    & winget @args
    if ($LASTEXITCODE -eq 0) {
        $script:Actions.Add("安装 $DisplayName（winget:$PackageId）")
        return $true
    }

    Write-WarnLine "winget 安装 $DisplayName 失败（exit=$LASTEXITCODE）"
    return $false
}

function Ensure-Tool(
    [string]$DisplayName,
    [string]$CommandName,
    [string]$WingetId,
    [string]$WingetOverrideArgs = ""
) {
    if (Test-CommandExists $CommandName) {
        $ver = Get-CommandVersionText $CommandName
        Write-Ok "$DisplayName 已就绪 ($ver)"
        return
    }

    Write-WarnLine "$DisplayName 未检测到"
    $canInstall = Confirm-Install $DisplayName
    if ($canInstall -and -not [string]::IsNullOrWhiteSpace($WingetId)) {
        $installed = Try-InstallWithWinget $WingetId $DisplayName $WingetOverrideArgs
        if ($installed -and (Test-CommandExists $CommandName)) {
            $ver = Get-CommandVersionText $CommandName
            Write-Ok "$DisplayName 安装成功 ($ver)"
            return
        }
    }

    $script:MissingItems.Add("$DisplayName ($CommandName)")
}

function Ensure-MsvcToolchain {
    if (Test-MsvcInstalled) {
        $vsPath = Get-VisualStudioInstallPath
        if ($null -ne $vsPath) {
            Write-Ok "MSVC 工具链已安装 ($vsPath)"
        } else {
            Write-Ok "MSVC 工具链已安装 (cl.exe in PATH)"
        }
        return
    }

    Write-WarnLine "MSVC C++ Build Tools 未检测到"
    $canInstall = Confirm-Install "Visual Studio 2022 Build Tools"
    if ($canInstall) {
        $override = "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart"
        $installed = Try-InstallWithWinget "Microsoft.VisualStudio.2022.BuildTools" "VS Build Tools" $override
        if ($installed -and (Test-MsvcInstalled)) {
            Write-Ok "MSVC 工具链安装成功"
            return
        }
    }

    $script:MissingItems.Add("MSVC C++ Build Tools")
}

function Ensure-VcpkgRoot([string]$ResolvedVcpkgRoot) {
    if (Test-Path $ResolvedVcpkgRoot) {
        Write-Ok "vcpkg 目录存在: $ResolvedVcpkgRoot"
        return $true
    }

    Write-WarnLine "未检测到 vcpkg 目录: $ResolvedVcpkgRoot"
    $canInstall = Confirm-Install "vcpkg"
    if (-not $canInstall) {
        $script:MissingItems.Add("vcpkg 目录 ($ResolvedVcpkgRoot)")
        return $false
    }

    if (-not (Test-CommandExists "git")) {
        Write-WarnLine "缺少 git，无法自动拉取 vcpkg"
        $script:MissingItems.Add("git (用于拉取 vcpkg)")
        return $false
    }

    $parent = Split-Path -Parent $ResolvedVcpkgRoot
    if (-not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    Write-Info "执行: git clone https://github.com/microsoft/vcpkg $ResolvedVcpkgRoot"
    & git clone https://github.com/microsoft/vcpkg $ResolvedVcpkgRoot
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ResolvedVcpkgRoot)) {
        Write-WarnLine "拉取 vcpkg 失败"
        $script:MissingItems.Add("vcpkg 目录 ($ResolvedVcpkgRoot)")
        return $false
    }

    $script:Actions.Add("拉取 vcpkg 到 $ResolvedVcpkgRoot")
    Write-Ok "vcpkg 拉取成功"
    return $true
}

function Ensure-VcpkgExe([string]$ResolvedVcpkgRoot) {
    $vcpkgExe = Join-Path $ResolvedVcpkgRoot "vcpkg.exe"
    if (Test-Path $vcpkgExe) {
        Write-Ok "vcpkg 可执行文件已就绪"
        return $vcpkgExe
    }

    $bootstrapBat = Join-Path $ResolvedVcpkgRoot "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapBat)) {
        Write-WarnLine "找不到 bootstrap-vcpkg.bat"
        $script:MissingItems.Add("vcpkg bootstrap 脚本")
        return $null
    }

    $canInstall = Confirm-Install "vcpkg bootstrap"
    if (-not $canInstall) {
        $script:MissingItems.Add("vcpkg.exe")
        return $null
    }

    Write-Info "执行: $bootstrapBat -disableMetrics"
    Push-Location $ResolvedVcpkgRoot
    try {
        & $bootstrapBat -disableMetrics
    } finally {
        Pop-Location
    }

    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $vcpkgExe)) {
        Write-WarnLine "vcpkg bootstrap 失败"
        $script:MissingItems.Add("vcpkg.exe")
        return $null
    }

    $script:Actions.Add("bootstrap vcpkg")
    Write-Ok "vcpkg bootstrap 成功"
    return $vcpkgExe
}

function Test-VcpkgPackageInstalled([string]$VcpkgExe, [string]$PackageName, [string]$TripletName) {
    $spec = "$PackageName`:$TripletName"
    $output = & $VcpkgExe list $spec 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $false
    }
    foreach ($line in $output) {
        if ($line -match ("^{0}\s" -f [regex]::Escape($spec))) {
            return $true
        }
    }
    return $false
}

function Ensure-VcpkgPackage([string]$VcpkgExe, [string]$PackageName, [string]$TripletName) {
    $spec = "$PackageName`:$TripletName"
    if (Test-VcpkgPackageInstalled $VcpkgExe $PackageName $TripletName) {
        Write-Ok "vcpkg 包已安装: $spec"
        return
    }

    Write-WarnLine "缺少 vcpkg 包: $spec"
    $canInstall = Confirm-Install "vcpkg 包 $spec"
    if (-not $canInstall) {
        $script:MissingItems.Add("vcpkg package $spec")
        return
    }

    Write-Info "执行: $VcpkgExe install $spec --recurse"
    & $VcpkgExe install $spec --recurse
    if ($LASTEXITCODE -ne 0) {
        Write-WarnLine "安装 vcpkg 包失败: $spec"
        $script:MissingItems.Add("vcpkg package $spec")
        return
    }

    if (Test-VcpkgPackageInstalled $VcpkgExe $PackageName $TripletName) {
        $script:Actions.Add("安装 vcpkg 包 $spec")
        Write-Ok "安装完成: $spec"
    } else {
        $script:MissingItems.Add("vcpkg package $spec")
    }
}

function Print-BuildHints([string]$RepoRoot, [string]$ResolvedVcpkgRoot, [string]$TripletName) {
    $serverDir = Join-Path $RepoRoot "server"
    $buildDir = Join-Path $serverDir "build-win-debug"
    $toolchain = Join-Path $ResolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    $useNinja = Test-CommandExists "ninja"
    $generator = if ($useNinja) { "Ninja" } else { "Visual Studio 17 2022" }

    Write-Section "Windows 构建命令（建议）"
    Write-Host "cmake -S `"$serverDir`" -B `"$buildDir`" -G `"$generator`" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=`"$toolchain`" -DVCPKG_TARGET_TRIPLET=$TripletName"
    Write-Host "cmake --build `"$buildDir`" -j"
    Write-Host "ctest --test-dir `"$buildDir`" --output-on-failure"

    if ($useNinja) {
        Write-Info "使用 Ninja + MSVC 时，请优先在 “x64 Native Tools Command Prompt for VS 2022” 内执行。"
    } else {
        Write-Info "未检测到 Ninja，已给出 Visual Studio 生成器方案。"
    }
}

function Run-BuildCheck([string]$RepoRoot, [string]$ResolvedVcpkgRoot, [string]$TripletName) {
    Write-Section "执行构建检查"
    if (-not (Test-CommandExists "cmake")) {
        Write-WarnLine "缺少 cmake，跳过构建检查"
        return
    }

    $serverDir = Join-Path $RepoRoot "server"
    $buildDir = Join-Path $serverDir "build-win-check"
    $toolchain = Join-Path $ResolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    $generator = if (Test-CommandExists "ninja") { "Ninja" } else { "Visual Studio 17 2022" }

    & cmake -S $serverDir -B $buildDir -G $generator `
        -DCMAKE_BUILD_TYPE=Debug `
        -DCMAKE_TOOLCHAIN_FILE=$toolchain `
        -DVCPKG_TARGET_TRIPLET=$TripletName
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败"
    }

    & cmake --build $buildDir
    if ($LASTEXITCODE -ne 0) {
        throw "构建失败"
    }

    & ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "测试失败"
    }

    Write-Ok "构建检查通过"
}

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw "该脚本仅支持 Windows 环境执行。"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = Join-Path $repoRoot ".tools\vcpkg"
}

Write-Section "LawnMowerServer Windows 环境检查"
Write-Info "仓库根目录: $repoRoot"
Write-Info "vcpkg 目录: $VcpkgRoot"
Write-Info "目标 triplet: $Triplet"
Write-Info "自动安装: $AutoInstall"

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
if (-not $isAdmin) {
    $script:Warnings.Add("当前不是管理员权限，部分安装可能失败。")
    Write-WarnLine "当前不是管理员权限，部分安装可能失败。"
}

Write-Section "系统工具检查"
Ensure-Tool -DisplayName "Git" -CommandName "git" -WingetId "Git.Git"
Ensure-Tool -DisplayName "CMake" -CommandName "cmake" -WingetId "Kitware.CMake"
Ensure-Tool -DisplayName "Ninja" -CommandName "ninja" -WingetId "Ninja-build.Ninja"
Ensure-MsvcToolchain

if (-not $SkipVcpkg) {
    Write-Section "vcpkg 依赖检查"
    $hasVcpkgRoot = Ensure-VcpkgRoot -ResolvedVcpkgRoot $VcpkgRoot
    if ($hasVcpkgRoot) {
        $vcpkgExe = Ensure-VcpkgExe -ResolvedVcpkgRoot $VcpkgRoot
        if ($null -ne $vcpkgExe) {
            Ensure-VcpkgPackage -VcpkgExe $vcpkgExe -PackageName "protobuf" -TripletName $Triplet
            Ensure-VcpkgPackage -VcpkgExe $vcpkgExe -PackageName "spdlog" -TripletName $Triplet
        }
    }
} else {
    Write-Info "已跳过 vcpkg 检查（--SkipVcpkg）"
}

Print-BuildHints -RepoRoot $repoRoot -ResolvedVcpkgRoot $VcpkgRoot -TripletName $Triplet

if ($RunBuildCheck -and ($script:MissingItems.Count -eq 0)) {
    Run-BuildCheck -RepoRoot $repoRoot -ResolvedVcpkgRoot $VcpkgRoot -TripletName $Triplet
}

Write-Section "检查结果"
if ($script:Actions.Count -gt 0) {
    Write-Host "已执行安装/初始化动作："
    foreach ($a in $script:Actions) {
        Write-Host "  - $a"
    }
}

if ($script:Warnings.Count -gt 0) {
    Write-Host "警告："
    foreach ($w in $script:Warnings) {
        Write-Host "  - $w"
    }
}

if ($script:MissingItems.Count -gt 0) {
    Write-Host "缺失项："
    foreach ($m in $script:MissingItems) {
        Write-Host "  - $m"
    }
    Write-WarnLine "环境检查未通过，请补齐缺失项后重试。"
    exit 1
}

Write-Ok "环境检查通过，可在 Windows 上构建并运行服务器。"
exit 0
