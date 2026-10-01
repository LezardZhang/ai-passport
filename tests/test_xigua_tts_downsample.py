#!/usr/bin/env python3
"""Independent FIR reference, speech-band gain and rejected alias tests."""
import ctypes
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

class State(ctypes.Structure):
    _fields_ = [('history', ctypes.c_int16 * 31), ('next', ctypes.c_uint),
                ('phase', ctypes.c_bool)]

def coefficients():
    taps = []
    for i in range(31):
        x = i - 15
        sinc = .4 if not x else math.sin(.4 * math.pi * x) / (math.pi * x)
        taps.append(sinc * (.54 - .46 * math.cos(2 * math.pi * i / 30)))
    total = sum(taps)
    result = [round(v / total * 32768) for v in taps]
    result[15] += 32768 - sum(result)
    return result

def main():
    with tempfile.TemporaryDirectory(prefix='xigua-downsample-') as temp:
        lib = Path(temp) / 'resample.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-Imain', 'main/xigua_tts_downsample.c', '-o', str(lib)],
                       cwd=ROOT, check=True)
        codec = ctypes.CDLL(str(lib))
        push = codec.xigua_tts_downsample_push
        push.argtypes = [ctypes.POINTER(State), ctypes.c_int16, ctypes.POINTER(ctypes.c_int16)]
        push.restype = ctypes.c_bool
        def process(signal):
            state = State()
            output = []
            sample = ctypes.c_int16()
            # Simulate irregular transport boundaries, including odd sample counts.
            at = 0
            lengths = (1, 7, 512, 3, 191)
            part = 0
            while at < len(signal):
                end = min(len(signal), at + lengths[part % len(lengths)])
                for value in signal[at:end]:
                    if push(ctypes.byref(state), value, ctypes.byref(sample)):
                        output.append(sample.value)
                at = end
                part += 1
            assert len(output) == len(signal) // 2
            return output
        rng = random.Random(12000)
        signal = [rng.randrange(-32768, 32768) for _ in range(1001)]
        taps = coefficients()
        expected = []
        for i in range(1, len(signal), 2):
            acc = sum(taps[k] * signal[i-k] for k in range(min(i+1, 31)))
            expected.append(max(-32768, min(32767, int(acc / 32768))))
        assert process(signal) == expected
        for level in (-32768, 32767):
            assert all(v == level for v in process([level] * 1000)[20:])
        # Three seconds at the service's rate remains three seconds at playback.
        assert len(process([0] * 72000)) == 36000
        gains = {}
        for frequency in (440, 1300, 4000, 7000, 8000, 10000):
            signal = [round(12000 * math.sin(2*math.pi*frequency*i/24000)) for i in range(24000)]
            out = process(signal)[100:]
            rms = math.sqrt(sum(v*v for v in out)/len(out))
            gains[frequency] = 20 * math.log10(max(rms, .01)/(12000/math.sqrt(2)))
        assert all(-1 < gains[f] < .1 for f in (440, 1300, 4000)), gains
        assert all(gains[f] < -45 for f in (7000, 8000, 10000)), gains
        print('24-to-12 kHz FIR: exact duration, chunk/tail continuity, full-scale bounds, speech gain and alias rejection: PASS')

if __name__ == '__main__':
    main()
