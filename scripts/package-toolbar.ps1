# Packages the current compiled toolbar build with a double-click installer.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$package = Join-Path $repo 'dist\WinDiaTweaks-v1.2-toolbar'
$payload = Join-Path $package 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($name in @('diatweaks.exe', 'DiaLauncher.exe', 'DiaTweaksTap.dll', 'tweaks.conf')) {
    Copy-Item -LiteralPath (Join-Path $repo "bin\$name") -Destination $payload
}
foreach ($name in @('uninstall.cmd', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $payload
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install-toolbar.cmd') -Destination (Join-Path $package 'Install.cmd')
$version = & (Join-Path $repo 'bin\diatweaks.exe') version
if ($LASTEXITCODE -ne 0) { throw 'Could not read the built version.' }
$commit = & git -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not read the source commit.' }
@"
WinDiaTweaks v1.2 with the current custom toolbar work
Build: $version
Source commit: $commit
Official WinDiaTweaks updates are disabled for this custom package.
Install a newer custom package to update it and keep the toolbar changes.
"@ | Set-Content -LiteralPath (Join-Path $payload 'custom-build.txt') -Encoding UTF8
@'
WinDiaTweaks v1.2 + custom toolbar

1. Install Dia on a Windows 11 x64 PC and open it once.
2. Extract this ZIP completely. Keep Install.cmd and payload together.
3. Double-click Install.cmd as your normal Windows user.
4. Open Dia (Tweaks), then Settings > Patches.

Installs to %USERPROFILE%\WinDiaTweaks-Toolbar\bin.
No Git, GitHub sign-in, or C++ build tools are needed.
Dia closes and reopens during installation. The Dia (Tweaks) shortcuts point
to this build afterward. Existing files in other WinDiaTweaks folders stay there.

This package includes the toolbar preferences copied from the original PC.
Installing over this custom package preserves the destination's preferences.
Clean search box is a new option and starts switched off.

Official WinDiaTweaks updates are disabled for this custom build so that they
cannot replace the toolbar changes. Install a newer custom package to update.
The combined UI still needs a live visual check in Dia.
'@ | Set-Content -LiteralPath (Join-Path $package 'Read me.txt') -Encoding UTF8
$archive = Join-Path $repo 'dist\WinDiaTweaks-v1.2-toolbar.zip'
Compress-Archive -Path (Join-Path $package 'Install.cmd'), (Join-Path $package 'Read me.txt'), $payload -DestinationPath $archive -Force
Write-Host "Created $archive"
