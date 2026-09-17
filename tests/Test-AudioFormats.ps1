<#
.SYNOPSIS
    End-to-end check of the endpoint on a target: which exclusive-mode formats it accepts, and whether what is
    played in each of several formats and modes comes back from the driver intact.

.DESCRIPTION
    Runs tests\AudioFormats.guest.ps1 in the target's console session (an audio client needs one), then, from the
    administrative session, fetches the captures the driver wrote -- the console user cannot read that folder --
    and checks each with wavcheck.py: sample rate, depth, signal length, and the tone on every channel.

    A capture that is short or has the wrong tones is a failure of the DRIVER as a measuring instrument, which is
    the point of running this after any change to it.

.PARAMETER Session
    An open administrative PSSession to the target, with the driver installed (tools\Install-VAudio.ps1).

.PARAMETER ConsoleUser
    The account logged on at the target's console. Default: whoever is.

.PARAMETER OutDir
    Where to put the fetched captures and the result JSON. Default: build\test-results\<timestamp>.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [System.Management.Automation.Runspaces.PSSession] $Session,
    [string] $ConsoleUser,
    [string] $OutDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) { $OutDir = Join-Path $root ("build\test-results\" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$captureDir = 'C:\Windows\System32\drivers\DriverData\Audio_Samples\SimpleAudioSample'

if (-not $ConsoleUser) {
    $ConsoleUser = ((Invoke-Command -Session $Session { (Get-CimInstance Win32_ComputerSystem).UserName }) -split '\\')[-1]
    if (-not $ConsoleUser) { throw 'Nobody is logged on at the target console; an audio client needs a desktop session.' }
}
$tag = Get-Date -Format 'HHmmss'

Copy-Item -ToSession $Session -Path (Join-Path $root 'build\out\x64\wasapiprobe.exe'), (Join-Path $PSScriptRoot 'AudioFormats.guest.ps1') -Destination 'C:\vaudio\' -Force

$guestJson = Invoke-Command -Session $Session -ArgumentList $tag, $ConsoleUser {
    param($tag, $user)
    $out = "C:\vaudio\formats-$tag.json"
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File C:\vaudio\AudioFormats.guest.ps1 -Out $out"
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive
    Register-ScheduledTask -TaskName 'VAudioFormats' -Action $action -Principal $principal -Force | Out-Null
    Start-ScheduledTask -TaskName 'VAudioFormats'
    $deadline = (Get-Date).AddSeconds(240)
    while (-not (Test-Path $out) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 3 }
    Unregister-ScheduledTask -TaskName 'VAudioFormats' -Confirm:$false
    if (Test-Path $out) { Get-Content $out -Raw }
}
if (-not $guestJson) { throw 'The guest script produced no result. Is the console user logged on?' }
Set-Content (Join-Path $OutDir 'guest.json') $guestJson
$guest = $guestJson | ConvertFrom-Json

# The guest script notes when each case started and stopped; the capture for a case is the file with data in it
# that was written in that window. The administrative session can see the folder, the console user cannot.
$files = @(Invoke-Command -Session $Session -ArgumentList $captureDir {
    param($dir)
    Get-ChildItem $dir -Filter *.wav | Where-Object Length -gt 1000 | ForEach-Object {
        [pscustomobject]@{ FullName = $_.FullName; Length = $_.Length; Written = $_.LastWriteTime.ToString('o') }
    }
})

$failed = 0
$summary = foreach ($case in $guest.cases) {
    $row = [ordered]@{ case = $case.name; result = $null; detail = $null }

    if ($case.expectRefusal) {
        if ($case.exit -ne 0) { $row.result = 'pass'; $row.detail = 'refused, as it should be' }
        else { $row.result = 'FAIL'; $row.detail = 'a format the driver does not list was accepted' }
    }
    elseif ($case.exit -ne 0) {
        $row.result = 'FAIL'; $row.detail = "could not play: $($case.message)"
    }
    else {
        $from = [datetime]$case.started; $to = ([datetime]$case.finished).AddSeconds(1)
        $hit = $files | Where-Object { $w = [datetime]$_.Written; $w -ge $from -and $w -le $to } | Sort-Object Written | Select-Object -Last 1
        if (-not $hit) {
            $row.result = 'FAIL'; $row.detail = 'no capture file was written during the case'
        } else {
            $local = Join-Path $OutDir ($case.name + '.wav')
            Copy-Item -FromSession $Session $hit.FullName $local -Force

            $f = $case.play.format
            $expect = (0..($f.channels - 1) | ForEach-Object { $case.play.baseHz + $_ * $case.play.stepHz }) -join ','
            $seconds = $case.play.toneFrames / $f.rate
            $checkArgs = @($local, '--expect', $expect, '--seconds', $seconds)
            # Shared mode goes through the engine, which renders in the mix format; only an exclusive stream
            # arrives at the device in the format it was written in.
            if ($case.play.mode -eq 'exclusive') { $checkArgs += @('--rate', $f.rate, '--bits', $f.bits) }

            $output = & python (Join-Path $PSScriptRoot 'wavcheck.py') @checkArgs
            if ($LASTEXITCODE -eq 0) { $row.result = 'pass'; $row.detail = ($output | Select-Object -First 1) }
            else { $row.result = 'FAIL'; $row.detail = (($output | Where-Object { $_ -match '^FAIL' }) -join '; ') }
        }
    }
    if ($row.result -ne 'pass') { $failed++ }
    [pscustomobject]$row
}

$supported = @($guest.formats.exclusive | Where-Object supported)
Write-Host ("Mix format: {0} Hz, {1} channels, {2}-bit {3}" -f $guest.formats.mixFormat.rate, $guest.formats.mixFormat.channels, $guest.formats.mixFormat.bits, $guest.formats.mixFormat.kind)
Write-Host ("Exclusive-mode formats accepted: {0} of {1} probed" -f $supported.Count, @($guest.formats.exclusive).Count)
$summary | Format-Table -AutoSize -Wrap | Out-Host
$summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir 'summary.json')
Write-Host "Results in $OutDir"

$global:LASTEXITCODE = 0
if ($failed) { throw "$failed audio case(s) failed." }
