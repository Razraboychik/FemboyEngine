param(
    [switch]$Install
)

$ErrorActionPreference = "Stop"

function Test-Tool {
    param([string]$Name, [switch]$Quiet)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        Write-Host "[ok]   $Name -> $($command.Source)"
        return $true
    }
    if (-not $Quiet) {
        Write-Host "[miss] $Name"
    }
    return $false
}

function Find-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $path = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -eq 0 -and $path) {
        return $path.Trim()
    }
    return $null
}

$missing = @()
$vs = Find-VisualStudio
$vsCMake = $null
$vsNinja = $null
if ($vs) {
    $vsCMake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    $vsNinja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
}

if (-not (Test-Tool "cmake" -Quiet)) {
    if ($vsCMake -and (Test-Path $vsCMake)) {
        Write-Host "[ok]   cmake (Visual Studio) -> $vsCMake"
    } else {
        Write-Host "[miss] cmake"
        $missing += "cmake"
    }
}
if (-not (Test-Tool "ninja" -Quiet)) {
    if ($vsNinja -and (Test-Path $vsNinja)) {
        Write-Host "[ok]   ninja (Visual Studio) -> $vsNinja"
    } else {
        Write-Host "[miss] ninja"
        $missing += "ninja"
    }
}
if (-not (Test-Tool "git")) { $missing += "git" }

if ($vs) {
    Write-Host "[ok]   Visual Studio C++ -> $vs"
} else {
    Write-Host "[miss] Visual Studio 2022 C++ workload"
    $missing += "visualstudio"
}

if ($Install) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is not available. Install App Installer from Microsoft Store."
    }
    if ($missing -contains "git") {
        winget install --id Git.Git -e --source winget --accept-source-agreements --accept-package-agreements
    }
    if ($missing -contains "cmake") {
        winget install --id Kitware.CMake -e --source winget --accept-source-agreements --accept-package-agreements
    }
    if ($missing -contains "ninja") {
        winget install --id Ninja-build.Ninja -e --source winget --accept-source-agreements --accept-package-agreements
    }
    if ($missing -contains "visualstudio") {
        winget install --id Microsoft.VisualStudio.2022.Community -e --source winget `
            --accept-source-agreements --accept-package-agreements `
            --override "--quiet --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
    }
    Write-Host ""
    Write-Host "Installers finished. Open a new terminal, then run:"
    Write-Host "  configure.bat"
    Write-Host "  build.bat"
} else {
    if ($missing.Count -gt 0) {
        Write-Host ""
        Write-Host "Missing: $($missing -join ', ')"
        Write-Host "Run with -Install to let winget install the standard tools:"
        Write-Host "  powershell -ExecutionPolicy Bypass -File tools/setup_windows.ps1 -Install"
        exit 1
    }
    Write-Host ""
    Write-Host "All required Windows tools are available."
}
