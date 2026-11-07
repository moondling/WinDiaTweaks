[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$vcVersion = (Get-Content (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vcRoot = Join-Path $vs "VC\Tools\MSVC\$vcVersion"
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = (Get-ChildItem "$sdkRoot\Include\10.*" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
if (-not $sdkVersion) { throw 'Windows SDK not found' }
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
Push-Location $repo
try {
    $env:INCLUDE = "$vcRoot\include;$sdkRoot\Include\$sdkVersion\ucrt"
    $env:LIB = "$vcRoot\lib\x64;$sdkRoot\Lib\$sdkVersion\ucrt\x64;$sdkRoot\Lib\$sdkVersion\um\x64"
    if (-not (Test-Path -LiteralPath bin)) { New-Item -ItemType Directory -Path bin | Out-Null }
    & "$vcRoot\bin\Hostx64\x64\cl.exe" /nologo /utf-8 /std:c++20 /EHsc /MT tests\address-text.cpp /Fobin\address-text-test.obj /Febin\address-text-test.exe
    if ($LASTEXITCODE -ne 0) { throw "Address display test compilation failed ($LASTEXITCODE)" }
    & .\bin\address-text-test.exe
    if ($LASTEXITCODE -ne 0) { throw "Address display tests failed ($LASTEXITCODE)" }
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
    Pop-Location
}
