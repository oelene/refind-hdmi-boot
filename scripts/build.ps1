[CmdletBinding()]
param(
    [string]$VisualStudioPath,
    [string]$WindowsSdkRoot,
    [switch]$Diagnostics
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
    $VisualStudioPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
}
if (-not $VisualStudioPath) { throw 'No x64 MSVC installation found.' }
$vc = Get-ChildItem -LiteralPath (Join-Path $VisualStudioPath 'VC\Tools\MSVC') -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $vc) { throw 'MSVC x64 compiler missing.' }
if (-not $WindowsSdkRoot) { $WindowsSdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10 }
$sdk = Get-ChildItem -LiteralPath (Join-Path $WindowsSdkRoot 'Include') -Directory |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdio.h')) -and
        (Test-Path -LiteralPath (Join-Path $WindowsSdkRoot ('Lib\'+$_.Name+'\um\x64\kernel32.lib')))
    } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK with x64 UCRT/UM libraries missing.' }
$cl = Join-Path $vc.FullName 'bin\Hostx64\x64\cl.exe'
$link = Join-Path $vc.FullName 'bin\Hostx64\x64\link.exe'
$include = Join-Path $repo 'third_party\edk2\MdePkg\Include'
$out = Join-Path $repo 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$programs = @('hdmi-auto')
if ($Diagnostics) { $programs += @('hdmi-menu','target-output') }
Push-Location (Join-Path $repo 'src')
try {
    foreach ($program in $programs) {
        & $cl /nologo /c /W4 /WX /GS- /Zl /Od /Oi- "/I$include" "/I$include\X64" "$program.c" "/Fo$out\$program.obj"
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $program" }
        & $link /nologo /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /fixed:no "/out:$out\$program.efi" "$out\$program.obj"
        if ($LASTEXITCODE -ne 0) { throw "EFI link failed: $program" }
        $bytes = [IO.File]::ReadAllBytes((Join-Path $out "$program.efi"))
        $pe = [BitConverter]::ToInt32($bytes,60)
        $opt = $pe + 24
        if ([BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664 -or
            [BitConverter]::ToUInt16($bytes,$opt+68) -ne 10 -or
            [BitConverter]::ToUInt32($bytes,$opt+120) -ne 0 -or
            [BitConverter]::ToUInt32($bytes,$opt+156) -eq 0) { throw "Unexpected EFI PE layout: $program" }
        (Get-FileHash (Join-Path $out "$program.efi")).Hash | Set-Content (Join-Path $out "$program.sha256") -Encoding ascii
    }
    $suites = @(
        @{Name='auto'; Defines=@('/DHDMI_MENU_TEST','/DHDMI_AUTO_TEST')},
        @{Name='menu'; Defines=@('/DHDMI_MENU_TEST')},
        @{Name='diagnostic'; Defines=@()}
    )
    foreach ($suite in $suites) {
        $defines = $suite.Defines
        $testName = 'test-' + $suite.Name
        & $cl /nologo /W4 /WX @defines "/I$include" "/I$include\X64" "/I$($vc.FullName)\include" "/I$($sdk.FullName)\ucrt" test-target.c "/Fo$out\$testName.obj" "/Fe$out\$testName.exe" /link "/LIBPATH:$($vc.FullName)\lib\x64" "/LIBPATH:$WindowsSdkRoot\Lib\$($sdk.Name)\ucrt\x64" "/LIBPATH:$WindowsSdkRoot\Lib\$($sdk.Name)\um\x64"
        if ($LASTEXITCODE -ne 0) { throw "Host test build failed: $testName" }
        & (Join-Path $out "$testName.exe")
        if ($LASTEXITCODE -ne 0) { throw "Host test failed: $testName" }
    }
    Write-Host "Built and checked in $out. No EFI partition or firmware settings were modified."
} finally { Pop-Location }
