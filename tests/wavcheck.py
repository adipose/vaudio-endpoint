"""Reads a WAV the driver captured and says what is in it.

    python wavcheck.py capture.wav
    python wavcheck.py capture.wav --expect 400,700,1000 --rate 48000 --bits 24 --seconds 2

Prints format, where the signal starts and ends, and the dominant frequency and peak level of each channel.
With --expect (one frequency per channel, in channel order) and the optional --rate/--bits/--seconds it also
checks them and exits 1 on any mismatch, so it can be the assertion at the end of a test.

The capture is WAVE_FORMAT_EXTENSIBLE, which Python's own wave module rejects before 3.12; hence the parsing here.
"""
import argparse
import struct
import sys

import numpy as np


def read_wav(path):
    b = open(path, 'rb').read()
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise SystemExit('%s is not a RIFF/WAVE file' % path)
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack('<I', b[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            fmt = b[pos + 8:pos + 8 + size]
        if cid == b'data':
            # The driver finalises the header from a work item; tolerate a size of zero and take what is there.
            data = b[pos + 8:pos + 8 + size] if size else b[pos + 8:]
            break
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise SystemExit('%s has no fmt or data chunk' % path)
    tag, channels, rate, _, align, bits = struct.unpack('<HHIIHH', fmt[:16])
    frames = len(data) // align
    data = data[:frames * align]
    if bits == 16:
        samples = np.frombuffer(data, dtype='<i2').reshape(-1, channels).astype(float) / 32768.0
    elif bits == 32:
        samples = np.frombuffer(data, dtype='<i4').reshape(-1, channels).astype(float) / 2147483648.0
    elif bits == 24:
        raw = np.frombuffer(data, dtype=np.uint8).reshape(-1, channels, 3).astype(np.int32)
        v = raw[:, :, 0] | (raw[:, :, 1] << 8) | (raw[:, :, 2] << 16)
        samples = np.where(v >= 1 << 23, v - (1 << 24), v).astype(float) / 8388608.0
    else:
        raise SystemExit('unsupported sample size: %d bits' % bits)
    return tag, channels, rate, bits, samples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--expect', help='comma-separated dominant frequency per channel, Hz')
    ap.add_argument('--rate', type=int)
    ap.add_argument('--bits', type=int)
    ap.add_argument('--seconds', type=float, help='expected length of the signal')
    ap.add_argument('--tolerance', type=float, default=2.0, help='Hz')
    args = ap.parse_args()

    tag, channels, rate, bits, s = read_wav(args.path)
    seconds = len(s) / rate
    print('tag=0x%04x channels=%d bits=%d rate=%d frames=%d seconds=%.2f' % (tag, channels, bits, rate, len(s), seconds))

    failures = []
    active = np.where(np.abs(s).max(axis=1) > 0.005)[0]
    if len(active) == 0:
        print('silent')
        sys.exit(1)
    start, end = active[0] / rate, active[-1] / rate
    print('signal from %.3fs to %.3fs (%.3fs)' % (start, end, end - start))

    # One second from a little after the start, or what there is; a Hann window keeps the peak clean.
    a = active[0] + rate // 10
    seg = s[a:a + rate]
    if len(seg) < rate // 4:
        seg = s[active[0]:active[-1]]
    freqs = np.fft.rfftfreq(len(seg), 1.0 / rate)
    window = np.hanning(len(seg))
    dominant = []
    for c in range(channels):
        spectrum = np.abs(np.fft.rfft(seg[:, c] * window))
        f = float(freqs[spectrum.argmax()])
        dominant.append(f)
        print('ch%d: dominant %.1f Hz, peak %.4f of full scale' % (c, f, float(np.abs(seg[:, c]).max())))

    if args.rate and rate != args.rate:
        failures.append('rate is %d, expected %d' % (rate, args.rate))
    if args.bits and bits != args.bits:
        failures.append('bits is %d, expected %d' % (bits, args.bits))
    if args.seconds and abs((end - start) - args.seconds) > 0.05:
        failures.append('signal lasts %.3fs, expected %.3fs' % (end - start, args.seconds))
    if args.expect:
        want = [float(x) for x in args.expect.split(',')]
        if len(want) != channels:
            failures.append('%d channels, expected %d' % (channels, len(want)))
        else:
            for c, (got, exp) in enumerate(zip(dominant, want)):
                if abs(got - exp) > args.tolerance:
                    failures.append('ch%d is %.1f Hz, expected %.1f' % (c, got, exp))

    for f in failures:
        print('FAIL: ' + f)
    if args.expect or args.rate or args.bits or args.seconds:
        print('PASS' if not failures else 'FAILED')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
