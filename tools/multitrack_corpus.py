"""Synthetic 1080p multitrack fixtures; generated locally, never user media."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import wave

ffmpeg, destination = sys.argv[1:]
root = Path(destination)
root.mkdir(parents=True, exist_ok=True)
for index, frequency in enumerate((240, 360, 480, 600)):
    with wave.open(str(root / f"audio-{index}.wav"), "wb") as audio:
        audio.setnchannels(2)
        audio.setsampwidth(2)
        audio.setframerate(48000)
        # Integral cycles in a second permit an independently calculable 12-second signal.
        second = b"".join(struct.pack("<hh", round(8192 * math.sin(2 * math.pi * frequency * n / 48000)),
                                     round(4096 * math.cos(2 * math.pi * frequency * n / 48000)))
                          for n in range(48000))
        audio.writeframes(second * 12)
for name, rate, extra in (
        ("video-a.mp4", "30000/1001", ["-c:v", "mpeg4", "-q:v", "4", "-bf", "2", "-g", "30"]),
        ("video-b.mkv", "30", ["-vf", "select=if(lt(t\\,6)\\,1\\,not(mod(n\\,2)))", "-fps_mode", "vfr",
                               "-c:v", "ffv1", "-level", "3", "-output_ts_offset", "2"])):
    subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                    f"testsrc2=size=1920x1080:rate={rate}:duration=12", *extra,
                    "-y", str(root / name)], check=True)
# Coarse Matroska packet timestamps and explicit alternate audio routing.
subprocess.run([ffmpeg, "-v", "error", "-i", str(root / "audio-0.wav"), "-i", str(root / "audio-1.wav"),
                "-map", "0:a", "-map", "1:a", "-c:a", "pcm_s16le", "-y", str(root / "multi-audio.mkv")], check=True)
# Mono / non-output sample rate exercises resampling and end flushing.
with wave.open(str(root / "mono-44100.wav"), "wb") as audio:
    audio.setnchannels(1)
    audio.setsampwidth(2)
    audio.setframerate(44100)
    audio.writeframes(b"".join(struct.pack("<h", round(8192*math.sin(2*math.pi*240*n/44100))) for n in range(44100)))
subprocess.run([ffmpeg, "-v", "error", "-i", str(root / "mono-44100.wav"), "-ac", "2", "-ar", "48000",
                "-f", "f32le", "-y", str(root / "mono-reference.f32")], check=True)
print("Multitrack corpus:", root.resolve())
