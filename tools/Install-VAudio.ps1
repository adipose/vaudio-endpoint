<#
.SYNOPSIS
    Installs (or updates, or removes) the virtual audio endpoint on a target,
    over an existing PowerShell session.

.DESCRIPTION
    Copies build\out\x64 to C:\vaudio on the target, trusts the test-signing
    certificate there, and creates the root-enumerated device
    ROOT\VAudioEndpoint with devcon -- or updates its driver if it exists.

    This is a kernel driver: the target must be in test-signing mode
    (bcdedit /set testsigning on, then reboot). The script checks and refuses
    rather than leaving a device that cannot start.

    Installing adds a render endpoint ("Speakers") and a capture endpoint
    (a tone generator). On a machine with no other audio device the render
    endpoint becomes the default, which is the point on a VM.

.PARAMETER Session
    An open PSSession to the target. If you run a claim broker, claim first.

.PARAMETER Remove
    Remove the device and the driver package instead.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [System.Management.Automation.Runspaces.PSSession] $Session,
    [switch] $Remove
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out = Join-Path $root 'build\out\x64'

if ($Remove) {
    Invoke-Command -Session $Session {
        $devcon = 'C:\vaudio\devcon.exe'
        if (Test-Path $devcon) { & $devcon remove 'ROOT\VAudioEndpoint' | Out-Host }
        Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match '\\vaudio\.inf' } |
            ForEach-Object { pnputil /delete-driver $_.Driver /uninstall /force | Out-Host }
    }
    return
}

foreach ($f in 'vaudio.sys', 'vaudio.inf', 'vaudio.cat', 'vaudio-test.cer', 'devcon.exe') {
    if (-not (Test-Path (Join-Path $out $f))) { throw "Missing $out\$f. Run tools\Build.ps1 first." }
}

$testSigning = Invoke-Command -Session $Session { [bool]((bcdedit /enum '{current}') -match 'testsigning\s+Yes') }
if (-not $testSigning) {
    throw 'The target is not in test-signing mode. Run "bcdedit /set testsigning on" there and reboot.'
}

Invoke-Command -Session $Session { New-Item -ItemType Directory -Force 'C:\vaudio' | Out-Null }
Copy-Item -ToSession $Session -Path (Join-Path $out '*') -Destination 'C:\vaudio\' -Force -Exclude '*.pdb'
$tools = Join-Path $out '..\tools'
if (Test-Path $tools) { Copy-Item -ToSession $Session -Path (Join-Path $tools '*') -Destination 'C:\vaudio\' -Force }

Invoke-Command -Session $Session {
    $ErrorActionPreference = 'Stop'
    Set-Location 'C:\vaudio'

    foreach ($store in 'Root', 'TrustedPublisher') {
        Import-Certificate -FilePath 'C:\vaudio\vaudio-test.cer' -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
    }

    $existing = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.HardwareID -contains 'ROOT\VAudioEndpoint' }
    if ($existing) {
        Write-Host 'Device exists; updating its driver ...'
        & .\devcon.exe update vaudio.inf 'ROOT\VAudioEndpoint' | Out-Host
    } else {
        Write-Host 'Creating device ROOT\VAudioEndpoint ...'
        & .\devcon.exe install vaudio.inf 'ROOT\VAudioEndpoint' | Out-Host
    }
    if ($LASTEXITCODE -notin 0, 1) { throw "devcon failed with exit code $LASTEXITCODE" }   # 1 = reboot suggested

    Start-Sleep -Seconds 3
    Get-PnpDevice | Where-Object { $_.HardwareID -contains 'ROOT\VAudioEndpoint' } |
        Format-Table FriendlyName, Status, Problem, InstanceId -AutoSize | Out-Host
    Get-PnpDevice -Class AudioEndpoint -PresentOnly -ErrorAction SilentlyContinue |
        Format-Table FriendlyName, Status -AutoSize | Out-Host
}
