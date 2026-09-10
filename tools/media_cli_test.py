"""Exercise the user-facing import/relink/save contract on generated media."""
import pathlib
import subprocess
import sys
import uuid

cli, probe, corpus = sys.argv[1:]
root = pathlib.Path(corpus)
project = root / ("cli-" + uuid.uuid4().hex + ".nle")

def run(*args, success=True):
    result = subprocess.run([cli, *map(str, args)], capture_output=True, text=True, encoding="utf-8", timeout=40)
    if (result.returncode == 0) != success:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout

try:
    run("new", project, "Unicode media test")
    assert "48000" in run("probe", root / "tone.wav", probe)
    run("import", project, root / "caf\u00e9 & tone.wav", probe)
    assert "status=available" in run("media-status", project)
    before = project.read_bytes()
    run("import", project, root / "corrupt.wav", probe, success=False)
    run("import", project, root / "tone.wav", root / "no-ffprobe", success=False)
    run("relink", project, "1x", root / "tone.wav", probe, success=False)
    run("relink", project, "1", root / "video.mp4", probe, success=False)
    assert project.read_bytes() == before
    run("relink", project, "1", root / "tone.wav", probe)
    assert "stream-ticks" in run("inspect", project)
    assert project.read_bytes().startswith(b"NLE_PROJECT 3")
finally:
    project.unlink(missing_ok=True)
