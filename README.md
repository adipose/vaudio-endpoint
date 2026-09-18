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

Exclusive mode and multichannel, same guest, same day, with
`tests\Test-AudioFormats.ps1` (which plays a different tone on every channel
through `wasapiprobe.exe` and checks the driver's capture with `wavcheck.py`):

| | |
|---|---|
| Exclusive-mode `IsFormatSupported` says yes to exactly the 30 formats the driver lists (stereo 44.1-192 kHz, 5.1 and 7.1 at 48 and 96 kHz, each at 16/24/32 bit) and no to the rest | yes |
| Shared stereo; exclusive stereo at 44.1/16, 96/24 and 192/32: right rate, depth, 2.000 s, right tone per channel | yes |
| Exclusive 5.1 at 48/16 and 7.1 at 48/24 and 96/32 (3 MB/s): every channel carries its own tone, nothing missing | yes |
| Exclusive 22.05 kHz, which the driver does not list, is refused with AUDCLNT_E_UNSUPPORTED_FORMAT | yes |

Getting there found a bug in Microsoft's sample worth knowing about if you
start from it: `CSaveData::WriteData` truncates any write longer than one
16 KB frame and discards the rest, and only the event-driven buffer path ever
enlarges the frame. A timer-driven exclusive stream at a high byte rate loses
the excess on every tick -- 3% of the audio at 576 KB/s, two thirds at 3 MB/s
-- and the result still looks like audio. Fixed here (see the comment at the
top of `src/Utilities/savedata.cpp`); the test above is what catches it.

Not yet proven: shared-mode multichannel (the mix format stays stereo until
the endpoint's speaker configuration is changed in Windows); bitstream
formats (AC-3, DTS), which the format table does not list.

A shared-mode capture contains everything the system played, not only the
program under test -- a notification sound lengthens it. Exclusive mode, or a
guest with system sounds off, avoids that.

## Where the audio goes

`C:\Windows\System32\drivers\DriverData\Audio_Samples\SimpleAudioSample\STREAM_HOST_<n>.wav`,
one file per render stream, `<n>` counting up from driver load. The audio
engine opens and closes a stream for every supported format when the endpoint
is first set up, which leaves a run of 68-byte header-only files; a test wants
the newest file with data in it. Set the driver's `DoNotCreateDataFiles`
registry value to 1 to turn capture off.

The capture folder is readable by administrators and SYSTEM only, so a test
fetches captures from an administrative session, not as the console user.

`tests/wavcheck.py` reads such a file (it is WAVE_FORMAT_EXTENSIBLE, which
Python's `wave` module rejects before 3.12) and reports format, where the
signal starts and ends, and the dominant frequency and peak level per channel;
with `--expect 400,700,...` it asserts them. `wasapiprobe.exe` (built alongside
the driver) is the matching player: `formats` lists what the endpoint accepts
in exclusive mode, `play` renders a tone per channel in a chosen format, shared
or exclusive.

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

## Licence

Two licences, by directory -- see `NOTICE`. The driver sources (`src/Main`,
`src/Filters`, `src/Inc`, `src/Utilities`) derive from Microsoft's
SimpleAudioSample and remain under the Microsoft Public License
(`LICENSE.microsoft-sample`), modifications included; the first commit is the
pristine sample, so the history shows exactly what changed. Everything else,
including the `wasapiprobe` client in `src/tools`, is MIT (`LICENSE`).
GitHub's licence badge can only show one of the two.
