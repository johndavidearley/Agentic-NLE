"""Linux packaging regression: preserve Qt FFmpeg's $ORIGIN stub dependencies.

Uses the checksum-verified archive cached by prepare_ffmpeg.py. This tests link
layout and safe repeat preparation, not ELF symbol resolution or real codecs.
"""
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

if platform.system() != "Linux":
    raise SystemExit("Run this packaging regression on Linux")

archive = Path(sys.argv[1]).resolve()
prepare = Path(__file__).with_name("prepare_ffmpeg.py")
with tempfile.TemporaryDirectory(prefix="nle-ffmpeg-layout-") as directory:
    root = Path(directory)
    qt = root / "qt"
    config = qt / "lib/cmake/Qt6/Qt6Config.cmake"
    config.parent.mkdir(parents=True)
    config.touch()
    for name, major in {"avutil": 59, "swresample": 5, "avcodec": 61,
                        "avformat": 61, "swscale": 8}.items():
        (qt / "lib" / f"lib{name}.so.{major}").touch()
    stubs = ["libQt6FFmpegStub-ssl.so.3", "libQt6FFmpegStub-crypto.so.3",
             "libQt6FFmpegStub-va.so.2", "libQt6FFmpegStub-va-drm.so.2",
             "libQt6FFmpegStub-va-x11.so.2"]
    for name in stubs:
        (qt / "lib" / name).touch()
    output = root / "prepared"
    output.mkdir()
    shutil.copyfile(archive, output / archive.name)
    for _ in range(2):
        subprocess.run([sys.executable, str(prepare), str(qt), str(output)], check=True)
        for name in stubs:
            link = output / "lib" / name
            assert link.is_symlink() and link.resolve() == qt / "lib" / name, name
    # A conflicting pre-existing entry must not be overwritten.
    conflict = output / "lib" / stubs[0]
    conflict.unlink()
    conflict.write_text("preserve me", encoding="utf-8")
    failed = subprocess.run([sys.executable, str(prepare), str(qt), str(output)],
                            capture_output=True, text=True)
    assert failed.returncode != 0 and "Refusing to replace" in failed.stderr
    assert conflict.read_text(encoding="utf-8") == "preserve me"
print("Linux FFmpeg stub layout, repeat preparation and conflict protection passed")
