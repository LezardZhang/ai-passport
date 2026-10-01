#!/usr/bin/env python3
"""Compare the bounded codec with Python's independent IMA ADPCM implementation."""
import hashlib
import ctypes
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# Golden vectors generated independently by CPython 3.9 audioop IMA ADPCM.
# Keep hashes in the test so Python 3.13+ does not need the removed audioop module.
GOLDEN = [('7939bf59abb76a237ebb17f7429416b180294aad40e59c2d88119c32d080cb63', (25452, 83), '752f2285da3c3e88c8d882c3174e241d2c429b4fb67333acc43aaf3ab9e4272c'), ('30ca242b3d47fc97ea59167ea24aaacb9f0852e50ed18a1b86fd242e77767edb', (-9852, 86), '9b0e1035c7e6b7d2d81ea899b43a3b4ae8425ec197426e2d761554d4a08e6f14'), ('bb7208bc9b5d7c04f1236a82a0093a5e33f40423d5ba8d4266f7092c3ba43b62', (16308, 84), 'b8b216056f1753e03d932e5a8c9bd6d921097836d742e8541f117f133517a4d8'), ('f0498ff964c38793192f9cbec4c918c020b49dae092dc4089a402579099f5b73', (28713, 86), '0e3bc0fd06bdc4d192604ab494888d1db5ba8914ea83977a64447fc16e2f6c3a'), ('5ea6de08257542525593709303d72c3e94b69d4ba06ad0a85cd7d82c8dae3032', (12074, 88), 'fb229951f0c72ac62f8145ab4d989a2c42abf9145938589180a8dec5022bb4c5'), ('2265eabb16edf57127e3b2965a8baa9c7d4a1359969813d657f54dae2b8afebb', (31437, 88), '642d19e69299faa0a9262449a39cc099ecf56b5cf7f1643cf4997f8c2aeb73d3')]
class State(ctypes.Structure):
    _fields_ = [('predictor', ctypes.c_int), ('index', ctypes.c_int)]
class Block(ctypes.Structure):
    _fields_ = [('predictor', ctypes.c_int16), ('pcm_bytes', ctypes.c_uint16),
                ('index', ctypes.c_uint8), ('data', ctypes.c_uint8 * 256)]

def main():
    with tempfile.TemporaryDirectory(prefix='xigua-adpcm-') as temp:
        lib = Path(temp) / 'codec.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-Imain', 'main/xigua_adpcm.c', '-o', str(lib)], cwd=ROOT, check=True)
        codec = ctypes.CDLL(str(lib))
        codec.xigua_adpcm_encode.argtypes = [ctypes.POINTER(State), ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Block)]
        codec.xigua_adpcm_encode.restype = ctypes.c_bool
        codec.xigua_adpcm_decode.argtypes = [ctypes.POINTER(Block), ctypes.c_void_p, ctypes.c_size_t]
        codec.xigua_adpcm_decode.restype = ctypes.c_bool
        rng = random.Random(20260930)
        state = State()
        for samples, (encoded_hash, expected_state, decoded_hash) in zip((512, 510, 2, 512, 126, 512), GOLDEN):
            # Use full-scale random PCM to exercise predictor/index saturation.
            pcm = b''.join(rng.randrange(-32768, 32768).to_bytes(2, 'little', signed=True) for _ in range(samples))
            block = Block()
            initial = (state.predictor, state.index)
            assert codec.xigua_adpcm_encode(ctypes.byref(state), pcm, len(pcm), ctypes.byref(block))
            assert (block.predictor, block.index) == initial
            assert hashlib.sha256(bytes(block.data[:samples//2])).hexdigest() == encoded_hash
            assert (state.predictor, state.index) == expected_state
            decoded = ctypes.create_string_buffer(len(pcm))
            assert codec.xigua_adpcm_decode(ctypes.byref(block), decoded, len(pcm))
            assert hashlib.sha256(decoded.raw).hexdigest() == decoded_hash
        # Independent blocks permit a partial tail with an odd sample count.
        pcm = b'\x00\x00' * 7
        block = Block()
        assert codec.xigua_adpcm_encode(ctypes.byref(state), pcm, len(pcm), ctypes.byref(block))
        decoded = ctypes.create_string_buffer(len(pcm))
        assert codec.xigua_adpcm_decode(ctypes.byref(block), decoded, len(pcm))
        assert hashlib.sha256(decoded.raw).hexdigest() == '4f3ebab324a7fbf04893ed324849c8ac025b7a0ddb25ef4dd7dd7d9f2f4f89c5'
        assert not codec.xigua_adpcm_decode(ctypes.byref(block), decoded, len(pcm)-1)
        block.index = 89
        assert not codec.xigua_adpcm_decode(ctypes.byref(block), decoded, len(pcm))
        assert not codec.xigua_adpcm_encode(ctypes.byref(state), pcm, 3, ctypes.byref(block))
        assert not codec.xigua_adpcm_encode(ctypes.byref(state), pcm, 1026, ctypes.byref(block))
        # Measure codec distortion on a speech-band signal across block boundaries.
        state = State()
        signal = [int(10000 * math.sin(2*math.pi*440*i/24000) + 3000*math.sin(2*math.pi*1300*i/24000)) for i in range(24000)]
        output = []
        for offset in range(0, len(signal), 512):
            pcm = b''.join(v.to_bytes(2, 'little', signed=True) for v in signal[offset:offset+512])
            block = Block(); decoded = ctypes.create_string_buffer(len(pcm))
            assert codec.xigua_adpcm_encode(ctypes.byref(state), pcm, len(pcm), ctypes.byref(block))
            assert codec.xigua_adpcm_decode(ctypes.byref(block), decoded, len(pcm))
            output.extend(int.from_bytes(decoded.raw[i:i+2], 'little', signed=True) for i in range(0,len(pcm),2))
        snr = 10*math.log10(sum(v*v for v in signal[512:])/sum((a-b)**2 for a,b in zip(signal[512:],output[512:])))
        assert snr > 25, snr
        print(f'ADPCM independent codec, partial tails, bounds, saturation and signal SNR {snr:.1f} dB: PASS')

if __name__ == '__main__':
    main()
