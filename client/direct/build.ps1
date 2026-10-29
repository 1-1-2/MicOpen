$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = "$root/client/build"
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools with Windows SDK are required' }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Push-Location $out
try {
    & cl.exe /nologo /LD /MT /O2 /std:c++20 /EHsc /utf-8 /W4 /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 "$PSScriptRoot/direct.cpp" /Femiclink-direct.dll /link windowsapp.lib runtimeobject.lib dwmapi.lib user32.lib gdi32.lib ole32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi Direct compilation failed' }
    & cl.exe /nologo /LD /MT /O2 /std:c++20 /EHsc /utf-8 /W4 /DMICLINK_DIRECT_TEST /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 "$PSScriptRoot/direct.cpp" /Fotest_direct.obj /Fetest_direct.dll /link windowsapp.lib runtimeobject.lib dwmapi.lib user32.lib gdi32.lib ole32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi Direct UI fixture compilation failed' }
} finally { Pop-Location }
