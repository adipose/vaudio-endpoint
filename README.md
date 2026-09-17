# vaudio-endpoint

A virtual sound card for testing software that plays audio. It is a kernel
audio driver (PortCls / WaveRT, from Microsoft's SimpleAudioSample) that gives
a machine with no audio hardware a working **Speakers** endpoint, and writes
everything played to that endpoint to a WAV file a test can read back.

It is the audio-side sibling of [bda-vtuner](https://github.com/adipose/bda-vtuner)
and idd-vdisplay: a virtual device at an OS boundary, whose result can be
asserted. With a generated clip that carries a different tone on each channel
or each audio track, a test can say which track actually played, on which
channels, for how long, at what level, in what format -- on a VM.

## What is proven

On a Hyper-V generation 2 guest running Windows 10 22H2 in test-signing mode,
which had **no audio endpoint at all** beforehand, 2026-09-17:

| | |
|---|---|
| Build with cl/link against the NuGet WDK, no WDK Visual Studio integration | yes |
| Install; "Speakers" and "Microphone Array" endpoints appear, audio service happy | yes |
| Play a 4.000 s stereo WAV (1000 Hz left, 2500 Hz right) through the shared-mode engine | yes |
| Captured file: 48 kHz 16-bit stereo, signal 4.000 s long, 1000.0 Hz on channel 0, 2500.0 Hz on channel 1 | yes |

Not yet proven: exclusive-mode negotiation against the extended format list
(44.1 to 192 kHz, 16/24/32-bit -- the sample offered only 48 kHz 16-bit);
anything beyond stereo, which also needs channel-configuration support in the
topology miniport; bitstream (AC-3, DTS) formats.

## Where the audio goes

`C:\Windows\System32\drivers\DriverData\Audio_Samples\SimpleAudioSample\STREAM_HOST_<n>.wav`,
one file per render stream, `<n>` counting up from driver load. The audio
engine opens and closes a stream for every supported format when the endpoint
is first set up, which leaves a run of 68-byte header-only files; a test wants
the newest file with data in it. Set the driver's `DoNotCreateDataFiles`
registry value to 1 to turn capture off.

`tests/wavcheck.py` reads such a file (it is WAVE_FORMAT_EXTENSIBLE, which
Python's `wave` module rejects before 3.12) and reports format, where the
signal starts and ends, and the dominant frequency and peak level per channel.

## Build

Needs Visual Studio's C++ tools and PowerShell. It does **not** need the WDK
installer or its Visual Studio extension: `Build.ps1` applies by hand what the
kernel-mode toolset would (`/kernel`, KMDF 1.25, `/DRIVER`, `FxDriverEntry`).

```powershell
.\tools\Install-Toolchain.ps1   # once; about 2.6 GB under build\toolchain
.\tools\Build.ps1               # build\out\x64: signed driver package
```

If idd-vdisplay (or anything else) has already fetched the same packages, set
`WDK_TOOLCHAIN_DIR` to that directory instead of downloading them again.

## Install

```powershell
. .\tools\GuestTransport.ps1
$s = Connect-TestGuest -Guest MyTestVM          # or any PSSession
.\tools\Install-VAudio.ps1 -Session $s
```

The target must be in test-signing mode (`bcdedit /set testsigning on`, then
reboot); the script checks first. `-Remove` takes the device and the driver
package out again. Built against the Windows 10 2004 DDI (the sample allocates
with `ExAllocatePool2`), so that is the oldest target.
