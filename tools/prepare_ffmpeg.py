"""Prepare FFmpeg 7.1 public headers/import libraries for the pinned Qt SDK.
No library source is built or committed. Run explicitly before CMake configuration.
"""
import argparse
import hashlib
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('qt', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
qt, output = args.qt.resolve(), args.output.resolve()
if not (qt/'lib/cmake/Qt6/Qt6Config.cmake').is_file():
    raise SystemExit('Expected an installed Qt SDK prefix')
if platform.machine().lower() not in ('amd64', 'x86_64', 'arm64', 'aarch64'):
    raise SystemExit('Only little-endian x64/ARM64 development hosts are supported')
output.mkdir(parents=True, exist_ok=True)
archive = output/'ffmpeg-7.1.3.tar.xz'
expected = 'f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe'
if not archive.exists():
    with urllib.request.urlopen('https://ffmpeg.org/releases/ffmpeg-7.1.3.tar.xz', timeout=60) as response:
        archive.write_bytes(response.read(32*1024*1024))
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('FFmpeg source archive checksum mismatch')
components = {'avutil': 59, 'swresample': 5, 'avcodec': 61, 'avformat': 61, 'swscale': 8}
include = output/'include'
with tarfile.open(archive) as source:
    for item in source.getmembers():
        parts = Path(item.name).parts
        if len(parts) != 3 or parts[0] != 'ffmpeg-7.1.3' or parts[1] not in {'lib'+x for x in components}:
            continue
        if not item.isfile() or not parts[2].endswith('.h'):
            continue
        target = include/parts[1]/parts[2]
        if not target.resolve().is_relative_to(include.resolve()):
            raise SystemExit('Invalid header path')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.extractfile(item).read())
(include/'libavutil/avconfig.h').write_text(
    '#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n'
    '#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n#endif\n', encoding='utf-8')
lib = output/'lib'
lib.mkdir(exist_ok=True)
if os.name == 'nt':
    tool = shutil.which('lib.exe')
    dumpbin = shutil.which('dumpbin.exe')
    if not tool or not dumpbin:
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        paths = sorted((Path(install)/'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/lib.exe'))
        if not paths:
            raise SystemExit('Install the MSVC x64 build tools')
        tool = str(paths[-1]); dumpbin = str(paths[-1].with_name('dumpbin.exe'))
    for name, major in components.items():
        dll = qt/'bin'/f'{name}-{major}.dll'
        if not dll.is_file():
            raise SystemExit(f'Missing matching Qt FFmpeg runtime: {dll}')
        exports = subprocess.check_output([dumpbin, '/exports', str(dll)], text=True)
        symbols = re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)\s*$', exports, re.MULTILINE)
        if not symbols:
            raise SystemExit(f'No exports in {dll}')
        definition = lib/f'{name}.def'
        definition.write_text(f'LIBRARY {dll.name}\nEXPORTS\n'+'\n'.join(symbols)+'\n', encoding='ascii')
        subprocess.run([tool, '/nologo', '/machine:x64', f'/def:{definition}', f'/out:{lib/name}.lib'], check=True)
else:
    def link(path: Path, target: Path):
        if path.is_symlink() and path.resolve() == target.resolve():
            return
        if path.exists() or path.is_symlink():
            raise SystemExit(f'Refusing to replace existing {path}')
        path.symlink_to(target.resolve())

    for name, major in components.items():
        runtime = qt/'lib'/(f'lib{name}.{major}.dylib' if platform.system() == 'Darwin' else f'lib{name}.so.{major}')
        if not runtime.is_file():
            raise SystemExit(f'Missing matching Qt FFmpeg runtime: {runtime}')
        link(lib/runtime.name, runtime)
        link(lib/(f'lib{name}.dylib' if platform.system() == 'Darwin' else f'lib{name}.so'),
             runtime)
print(f'Use -DNLE_FFMPEG_ROOT="{output}". Runtime libraries remain in the Qt SDK.')
