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

# Deliberately irregular video timestamps: 12 fps in the first second, ~5 fps later.
subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=60:duration=2",
                "-vf", "select='if(lt(t,1),not(mod(n,5)),not(mod(n,12)))'", "-fps_mode", "vfr",
                "-c:v", "ffv1", "-y", str(root / "vfr.mkv")], check=True)

# Shared origins, delayed audio/video and a longer A/V scheduling fixture.
for name, video_delay, audio_delay, video_duration, audio_duration in [
        ("offset-common.mkv", 0, 0, 4, 4),
        ("offset-audio.mkv", 0, 0.5, 4, 3.5),
        ("offset-video.mkv", 0.5, 0, 3.5, 4),
        ("long-av.mkv", 0, 0.5, 12, 11.5)]:
    subprocess.run([ffmpeg, "-v", "error", "-itsoffset", str(video_delay),
                    "-f", "lavfi", "-i", f"testsrc2=size=128x72:rate=24:duration={video_duration}",
                    "-itsoffset", str(audio_delay), "-f", "lavfi", "-i",
                    f"sine=frequency=440:sample_rate=48000:duration={audio_duration}",
                    "-c:v", "ffv1", "-c:a", "pcm_s16le", "-output_ts_offset", "2",
                    "-y", str(root / name)], check=True)
subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "testsrc2=size=128x72:rate=30000/1001:duration=4", "-c:v", "mpeg4",
                "-bf", "2", "-g", "48", "-y", str(root / "bframes.mp4")], check=True)

subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "testsrc2=size=96x64:rate=24:duration=4", "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=4", "-c:v", "ffv1", "-c:a", "pcm_s16le",
                "-output_ts_offset", "-1", "-avoid_negative_ts", "disabled", "-y",
                str(root / "negative-origin.mkv")], check=True)

subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=4", "-c:a", "pcm_s16le",
                "-output_ts_offset", "-1", "-avoid_negative_ts", "disabled", "-y",
                str(root / "negative-audio.mkv")], check=True)

# Independent stereo PCM oracle for the offset/negative-origin fixtures above.
subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=4",
                "-ac", "2", "-c:a", "pcm_f32le", "-f", "f32le", "-y",
                str(root / "origin-reference.f32")], check=True)
