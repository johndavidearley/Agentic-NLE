"""Generate a tiny, reproducible local corpus; no downloads or user media."""
import pathlib
import shutil
import subprocess
import sys
import wave

ffmpeg, destination = sys.argv[1:]
root = pathlib.Path(destination)
root.mkdir(parents=True, exist_ok=True)
for name, samples in [("tone.wav", 96000), ("short.wav", 24000)]:
    with wave.open(str(root / name), "wb") as audio:
        audio.setnchannels(2)
        audio.setsampwidth(2)
        audio.setframerate(48000)
        audio.writeframes(b"\0" * samples * 4)
shutil.copyfile(root / "tone.wav", root / "caf\u00e9 & tone.wav")
(root / "corrupt.wav").write_bytes(b"not a media container")
(root / "network.m3u8").write_text("#EXTM3U\n#EXTINF:2,\nhttps://example.invalid/segment.ts\n")
subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "testsrc2=size=64x48:rate=30000/1001", "-frames:v", "30", "-c:v", "mpeg4",
                "-y", str(root / "video.mp4")], check=True)
subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=64x48:rate=24",
                "-i", str(root / "tone.wav"), "-t", "2", "-c:v", "ffv1", "-c:a", "pcm_s16le",
                "-y", str(root / "av.mkv")], check=True)
