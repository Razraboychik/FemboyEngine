[CmdletBinding()]
param(
    [string]$Root = '',
    [string]$Build = '',
    [string]$ClangFormat = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe',
    [string]$ClangTidy = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe'
)

$ErrorActionPreference = 'Continue'

if ([string]::IsNullOrEmpty($Root))
{
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Root = Split-Path -Parent $scriptDir
}
if ([string]::IsNullOrEmpty($Build))
{
    $Build = Join-Path $Root 'build'
}

$sourceDirs = @(
    (Join-Path $Root 'engine\include\femboyengine'),
    (Join-Path $Root 'engine\src'),
    (Join-Path $Root 'Projects\NeonTriangle\src'),
    (Join-Path $Root 'tests')
)

$sourceExts = @('*.hpp', '*.cpp', '*.h', '*.cc')
$buildExts = @('*.cmake', 'CMakeLists.txt')

$sourceFiles = Get-ChildItem -Path $sourceDirs -Recurse -Include $sourceExts -File |
    Select-Object -ExpandProperty FullName
$buildFiles = @(Join-Path $Root 'CMakeLists.txt')
$cmakeDir = Join-Path $Root 'cmake'
if (Test-Path $cmakeDir)
{
    $buildFiles += Get-ChildItem -Path $cmakeDir -Recurse -Include $buildExts -File |
        Select-Object -ExpandProperty FullName
}
$allFiles = @($sourceFiles) + @($buildFiles)

Write-Host "Found $($sourceFiles.Count) source files and $($buildFiles.Count) build files."

$tidyLog = Join-Path $Root 'build\clang-tidy-format.log'
Write-Host "Running clang-tidy brace fixes..."
& $ClangTidy $sourceFiles -p $Build --checks='-*,readability-braces-around-statements' --fix --header-filter=.* *>&1 |
    Out-File -FilePath $tidyLog -Encoding utf8
Write-Host "clang-tidy log: $tidyLog"

Write-Host "Running clang-format..."
& $ClangFormat -i $allFiles
Write-Host "clang-format finished."

$decorativeComment = [regex]::new('(?m)^\s*//\s*[-=]{3,}\s*$')

foreach ($file in $allFiles)
{
    $content = [System.IO.File]::ReadAllText($file)
    $content = $decorativeComment.Replace($content, '')
    $content = $content.TrimEnd([char[]]"`r`n") + "`r`n`r`n"
    [System.IO.File]::WriteAllText($file, $content)
}

Write-Host "Trailing blank lines and decorative comments cleaned."
Write-Host "Done."
