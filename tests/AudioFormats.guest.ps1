# Runs on the target, in the console session. Asks the endpoint which exclusive-mode formats it accepts, then plays
# a per-channel tone through it in several formats and modes, noting when each case ran. The host matches the
# driver's capture files to the cases by time and checks them (Test-AudioFormats.ps1); the console user cannot
# read the capture folder itself. Writes one JSON object to -Out.
param(
    [string] $Out = 'C:\vaudio\audioformats.json'
)

$ErrorActionPreference = 'Continue'
Set-Location 'C:\vaudio'

$cases = @(
    @{ name = 'shared-stereo-48k16';      args = @('--rate', '48000', '--bits', '16', '--channels', '2') }
    @{ name = 'exclusive-stereo-44k16';   args = @('--exclusive', '--rate', '44100', '--bits', '16', '--channels', '2') }
    @{ name = 'exclusive-stereo-96k24';   args = @('--exclusive', '--rate', '96000', '--bits', '24', '--channels', '2') }
    @{ name = 'exclusive-stereo-192k32';  args = @('--exclusive', '--rate', '192000', '--bits', '32', '--channels', '2') }
    @{ name = 'exclusive-5.1-48k16';      args = @('--exclusive', '--rate', '48000', '--bits', '16', '--channels', '6') }
    @{ name = 'exclusive-7.1-48k24';      args = @('--exclusive', '--rate', '48000', '--bits', '24', '--channels', '8') }
    @{ name = 'exclusive-7.1-96k32';      args = @('--exclusive', '--rate', '96000', '--bits', '32', '--channels', '8') }
    @{ name = 'exclusive-stereo-22k16';   args = @('--exclusive', '--rate', '22050', '--bits', '16', '--channels', '2'); expectRefusal = $true }
)

$result = [ordered]@{}
$result.formats = (& .\wasapiprobe.exe formats | ConvertFrom-Json)
$result.cases = @()

foreach ($c in $cases) {
    $started = Get-Date
    $stdout = & .\wasapiprobe.exe play @($c.args) --seconds 2 2>&1
    $exit = $LASTEXITCODE
    Start-Sleep -Seconds 3      # the driver writes its file from a work item after the stream closes

    $parsed = $null
    if ($exit -eq 0) { try { $parsed = ($stdout | Select-Object -Last 1) | ConvertFrom-Json } catch { } }
    $result.cases += [ordered]@{
        name = $c.name
        expectRefusal = [bool]$c.expectRefusal
        exit = $exit
        started = $started.ToString('o')
        finished = (Get-Date).ToString('o')
        play = $parsed
        message = if ($exit -ne 0) { (($stdout | ForEach-Object { "$_" }) -join ' ').Trim() } else { $null }
    }
}

$result | ConvertTo-Json -Depth 8 | Set-Content $Out
