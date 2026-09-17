<#
.SYNOPSIS
    Builds the virtual audio endpoint driver (kernel mode, PortCls/WaveRT),
    then catalogues and test-signs the package.

.DESCRIPTION
    The WDK and a matching SDK come from NuGet (Install-Toolchain.ps1) and the
    compiler from whatever Visual Studio is installed. The WDK's Visual Studio
    integration is not needed: this script applies by hand what its
    WindowsKernelModeDriver10.0 toolset would -- /kernel, the km and KMDF include
    and library paths, /DRIVER with FxDriverEntry -- and calls cl.exe and link.exe.

    Output goes to build\out\x64: vaudio.sys, vaudio.inf, vaudio.cat and the
    public half of the signing certificate.

    A kernel driver only loads on a target in test-signing mode
    (bcdedit /set testsigning on) that trusts the certificate.

.PARAMETER CertName
    Subject CN of the test-signing certificate, created on first use.

.PARAMETER NoSign
    Build only.
#>
[CmdletBinding()]
param(
    [string] $CertName = 'vaudio-endpoint test signing',
    [switch] $NoSign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Toolchain.ps1')
$tc = Get-KitToolchain -Root $root

$out = Join-Path $root 'build\out\x64'
$obj = Join-Path $root 'build\obj\x64'
New-Item -ItemType Directory -Force $out, $obj | Out-Null

# --- Compiler environment ---------------------------------------------------

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ tools found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$envDump = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
$vcEnv = @{}
foreach ($line in $envDump) {
    $i = $line.IndexOf('=')
    if ($i -gt 0) { $vcEnv[$line.Substring(0, $i)] = $line.Substring($i + 1) }
}
$vcToolsDir = $vcEnv['VCToolsInstallDir']
if (-not $vcToolsDir) { throw "vcvars64.bat did not set VCToolsInstallDir ($vcvars)." }
$env:PATH = $vcEnv['PATH']

# Stamp a version that is unique to this build into the staged INF. PnP compares DriverVer before anything else:
# a rebuilt package with the same version is "already installed", and the old binary stays in the driver store
# while devcon reports success.
function Set-InfDriverVer {
    param([string] $Path)
    $now = Get-Date
    $ver = '1.{0}.{1}.{2}' -f (($now.Year - 2000) * 1000 + $now.DayOfYear), ($now.Hour * 100 + $now.Minute), $now.Second
    $stamp = 'DriverVer = {0},{1}' -f $now.ToString('MM/dd/yyyy', [Globalization.CultureInfo]::InvariantCulture), $ver
    $text = [IO.File]::ReadAllText($Path)
    $new = [regex]::Replace($text, '(?m)^DriverVer\s*=.*$', $stamp)
    if ($new -eq $text) { throw "No DriverVer line found in $Path" }
    [IO.File]::WriteAllText($Path, $new)
    Write-Host "Driver version $ver" -ForegroundColor Cyan
}

function Invoke-Tool {
    param([string] $Exe, [string[]] $Arguments, [string] $What)
    & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

$ver = $tc.KitVersion
$wdkInc = Join-Path $tc.WdkRoot "Include\$ver"
$sdkInc = Join-Path $tc.SdkRoot "Include\$ver"
$src = Join-Path $root 'src'

# The sample is a KMDF miniport driver: PortCls owns dispatch, the framework rides along. 1.25 is Windows 10 1803.
$kmdf = '1.25'

# Kernel code sees the WDK's km headers and its own CRT subset, never the user-mode CRT.
$env:INCLUDE = @(
    $src, (Join-Path $src 'Inc'), (Join-Path $src 'Main'), (Join-Path $src 'Filters'), (Join-Path $src 'Utilities'),
    (Join-Path $tc.WdkRoot "Include\wdf\kmdf\$kmdf"),
    (Join-Path $wdkInc 'km'), (Join-Path $wdkInc 'km\crt'), (Join-Path $wdkInc 'shared'), (Join-Path $sdkInc 'shared')
) -join ';'
$env:LIB = @((Join-Path $tc.WdkRoot "Lib\$ver\km\x64"), (Join-Path $tc.WdkRoot "Lib\wdf\kmdf\x64\$kmdf")) -join ';'

# src\tools holds user-mode test clients, built separately below.
$sources = Get-ChildItem $src -Recurse -Filter *.cpp | Where-Object { $_.FullName -notmatch '\\src\\tools\\' } | ForEach-Object FullName

Write-Host "Compiling vaudio.sys ($($sources.Count) files) ..." -ForegroundColor Cyan

$clArgs = @(
    '/nologo', '/c', '/kernel', '/W3', '/WX', '/wd4996', '/O2', '/Oi', '/Zi', '/GF', '/Gy', '/Zp8', '/GS', '/GR-',
    '/D_WIN64', '/D_AMD64_', '/DAMD64', '/D_WIN32', '/DUNICODE', '/D_UNICODE', '/DWINNT=1', '/DSTD_CALL',
    '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', '/DNTDDI_VERSION=0x0A000008',   # Windows 10 2004: the sample allocates with ExAllocatePool2
    '/DDEPRECATE_DDK_FUNCTIONS=1', '/DPOOL_NX_OPTIN=1', '/DNDEBUG',
    '/DKMDF_VERSION_MAJOR=1', "/DKMDF_VERSION_MINOR=$($kmdf.Split('.')[1])",
    '/DPC_IMPLEMENTATION', '/D_USE_WAVERT_', '/D_USE_IPortClsRuntimePower', '/D_NEW_DELETE_OPERATORS_', '/DDEBUG_LEVEL=DEBUGLVL_TERSE',
    "/Fo$obj/", "/Fd$obj/vaudio.pdb"
) + $sources
Invoke-Tool cl.exe $clArgs 'cl'

$objects = $sources | ForEach-Object { Join-Path $obj ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj') }

$linkArgs = @(
    '/nologo', '/DRIVER', '/SUBSYSTEM:NATIVE,10.00', '/ENTRY:FxDriverEntry', '/NODEFAULTLIB',
    '/DEBUG', '/OPT:REF', '/OPT:ICF', '/RELEASE', '/MERGE:_TEXT=.text;_PAGE=PAGE', '/SECTION:INIT,d',
    '/INTEGRITYCHECK', '/NXCOMPAT', '/DYNAMICBASE',
    "/OUT:$out\vaudio.sys", "/PDB:$out\vaudio.pdb",
    'WdfDriverEntry.lib', 'WdfLdr.lib',
    'bufferoverflowfastfailk.lib', 'ntoskrnl.lib', 'hal.lib', 'wmilib.lib',
    'portcls.lib', 'stdunk.lib', 'libcntpr.lib', 'ks.lib', 'ksguid.lib', 'ntstrsafe.lib'
) + $objects
Invoke-Tool link.exe $linkArgs 'link'

Copy-Item (Join-Path $src 'Main\vaudio.inf') $out -Force
Set-InfDriverVer (Join-Path $out 'vaudio.inf')

# --- Test client -------------------------------------------------------------
#
# wasapiprobe is an ordinary user-mode program: the VC runtime and the SDK's um headers, none of the km ones.

Write-Host 'Compiling wasapiprobe.exe ...' -ForegroundColor Cyan

$env:INCLUDE = @(
    (Join-Path $vcToolsDir 'include'),
    (Join-Path $sdkInc 'ucrt'), (Join-Path $sdkInc 'shared'), (Join-Path $sdkInc 'um')
) -join ';'
$env:LIB = @(
    (Join-Path $vcToolsDir 'lib\x64'),
    (Join-Path $tc.SdkLibRoot 'ucrt\x64'), (Join-Path $tc.SdkLibRoot 'um\x64')
) -join ';'

$clArgs = @(
    '/nologo', '/W4', '/WX', '/O2', '/EHsc', '/MT', '/std:c++17', '/permissive-',
    '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00',
    "/Fo$obj/", "/Fe$out/wasapiprobe.exe",
    (Join-Path $src 'tools\wasapiprobe.cpp'),
    '/link', '/SUBSYSTEM:CONSOLE', 'ole32.lib', 'avrt.lib'
)
Invoke-Tool cl.exe $clArgs 'cl (wasapiprobe)'

if ($NoSign) {
    Write-Host "Built (unsigned): $out" -ForegroundColor Green
    return
}

# --- Catalogue and sign -----------------------------------------------------

$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq "CN=$CertName" -and $_.NotAfter -gt (Get-Date) } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating test certificate CN=$CertName ..." -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate -Subject "CN=$CertName" -Type CodeSigningCert -KeyUsage DigitalSignature `
        -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5) -KeyExportPolicy Exportable
}
Export-Certificate -Cert $cert -FilePath (Join-Path $out 'vaudio-test.cer') -Force | Out-Null

$pkg = Join-Path $root 'build\package'
New-Item -ItemType Directory -Force $pkg | Out-Null
Get-ChildItem $pkg | ForEach-Object { [IO.File]::Delete($_.FullName) }
Copy-Item (Join-Path $out 'vaudio.sys'), (Join-Path $out 'vaudio.inf') $pkg

Write-Host 'Building catalogue ...' -ForegroundColor Cyan
Invoke-Tool $tc.Inf2Cat @("/driver:$pkg", '/os:10_X64', '/uselocaltime') 'inf2cat'

foreach ($f in @('vaudio.cat', 'vaudio.sys')) {
    Invoke-Tool $tc.SignTool @('sign', '/fd', 'SHA256', '/sha1', $cert.Thumbprint, '/s', 'My', (Join-Path $pkg $f)) "signtool ($f)"
}
Copy-Item (Join-Path $pkg '*') $out -Force
Copy-Item $tc.DevCon $out -Force

Write-Host "Built and signed: $out" -ForegroundColor Green
