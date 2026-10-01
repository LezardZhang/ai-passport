#!/usr/bin/env python3
"""Create the small, reproducible nursery audio seed set.

The generated files are deliberately plain PCM WAV: two original Chinese
story readings and three short synthesized arrangements of public-domain works.
No commercial recording is copied into the repository or uploaded by this
script.  macOS ``say`` and ffmpeg are used for the story voice when available;
the classical examples are produced with only Python's standard library.
"""
from __future__ import annotations

import hashlib
import json
import math
import shutil
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "seed-audio"
RATE = 12_000

STORIES = {
    "story-little-ant-and-leaf": ("小蚂蚁和一片叶子", "小蚂蚁在雨后的路边发现一片大叶子。它请来伙伴，把叶子当作小船，顺着浅浅的水沟回到树下。大家明白了，遇到困难时，先想办法，再一起帮忙。"),
    "story-little-star-lost-way": ("迷路的小星星", "一颗小星星找不到回家的路。月亮请它慢慢观察云的方向，萤火虫为它照亮树梢。小星星学会了安静、耐心地寻找，终于回到夜空中。"),
}

MELODIES = {
    "classical-bach-c-major": ("巴赫：C 大调前奏曲（早教合成版）", [261.63, 329.63, 392.00, 523.25, 392.00, 329.63, 261.63, 196.00]),
    "classical-mozart-twinkle": ("莫扎特：小星星主题（早教合成版）", [261.63, 261.63, 392.00, 392.00, 440.00, 440.00, 392.00, 349.23, 349.23, 329.63, 329.63, 293.66, 293.66, 261.63]),
    "classical-beethoven-ode": ("贝多芬：欢乐颂主题（早教合成版）", [329.63, 329.63, 349.23, 392.00, 392.00, 349.23, 329.63, 293.66, 261.63, 261.63, 293.66, 329.63, 329.63, 293.66, 293.66]),
}


def write_tone(path: Path, notes: list[float], seconds: float = 0.42) -> None:
    frames: list[int] = []
    for i, frequency in enumerate(notes):
        length = int(RATE * seconds)
        for n in range(length):
            t = n / RATE
            envelope = min(1.0, n / (RATE * 0.025), (length - n) / (RATE * 0.05))
            sample = int(11_000 * envelope * math.sin(2 * math.pi * frequency * t))
            frames.append(sample)
        frames.extend([0] * int(RATE * 0.04))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(RATE)
        wav.writeframes(struct.pack("<%dh" % len(frames), *frames))


def write_story(path: Path, text: str) -> None:
    say = shutil.which("say")
    ffmpeg = shutil.which("ffmpeg")
    if not say or not ffmpeg:
        raise SystemExit("story generation requires macOS say and ffmpeg")
    with tempfile.TemporaryDirectory(prefix="xigua-story-") as tmp:
        aiff = Path(tmp) / "story.aiff"
        subprocess.run([say, "-v", "Tingting", "-o", str(aiff), text], check=True)
        subprocess.run([ffmpeg, "-y", "-v", "error", "-i", str(aiff), "-ac", "1", "-ar", str(RATE), "-c:a", "pcm_s16le", str(path)], check=True)


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    entries = []
    for ident, (title, text) in STORIES.items():
        filename = ident + ".wav"; path = ROOT / filename
        write_story(path, text)
        entries.append({"id": ident, "title": title, "category": "story", "file": filename,
                        "attribution": {"source_url": "https://wiki.librivox.org/index.php?title=Copyright_and_Public_Domain", "creator": "西瓜育儿原创中文朗读（公版寓言改写）", "license": "Public Domain Mark 1.0", "license_url": "https://creativecommons.org/publicdomain/mark/1.0/", "changes": "原创中文改写并转换为 12 kHz 单声道 PCM WAV"}})
    for ident, (title, notes) in MELODIES.items():
        filename = ident + ".wav"; path = ROOT / filename
        write_tone(path, notes)
        entries.append({"id": ident, "title": title, "category": "classical", "file": filename,
                        "attribution": {"source_url": "https://creativecommons.org/publicdomain/mark/1.0/", "creator": "西瓜育儿原创合成演奏；作曲：巴赫／莫扎特／贝多芬", "license": "Public Domain Mark 1.0", "license_url": "https://creativecommons.org/publicdomain/mark/1.0/", "changes": "公版作品主题的短篇原创合成编配，转换为 12 kHz 单声道 PCM WAV"}})
    for item in entries:
        data = (ROOT / item["file"]).read_bytes()
        item["sha256"] = hashlib.sha256(data).hexdigest()
        with wave.open(str(ROOT / item["file"]), "rb") as wav:
            item["duration_ms"] = round(wav.getnframes() * 1000 / wav.getframerate())
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, RATE)
    (ROOT / "manifest.json").write_text(json.dumps(entries, ensure_ascii=False, indent=2) + "\n")
    print(f"generated {len(entries)} tracks in {ROOT}")


if __name__ == "__main__":
    main()
