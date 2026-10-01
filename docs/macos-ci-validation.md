# macOS validation of the export callback CI fix

On 2026-10-01, the full macOS Intel Release desktop build and **53/53 CTest
tests passed** with `NLE_WARNINGS_AS_ERRORS=ON`. The exact CI formatter,
clang-format 19.1.1, passed across all C++ sources. [Saved results](macos-ci-results.json)
include the CTest output and generated evaluation reports.

The tested tree is commit `e1ce26a86adb16dedbbb34f130aef62f1be88aa9` plus the
explicit empty default for `Options::validate_destination`. Both failing caller
translation units, `src/export/main.cpp` and `src/desktop/window_settings.cpp`,
compiled successfully without weakening compiler warnings.

The host was macOS 15.7.9 (24G830), x86_64, using AppleClang 17.0.0.17000604,
Qt 6.10.3 installed by aqtinstall 3.3.0, checksum-verified FFmpeg 7.1.3 headers
and the Qt SDK's matching runtime libraries. Python 3.11.15 and MCP SDK 2.2.0
ran the official client acceptance tests. The local Python environment used
cryptography 46.0.3, whose Intel wheel satisfies the SDK dependency.

Coverage includes media import/relink, MCP export and cancellation, recovery,
timeline workflows at 100%, 150% and 200% scaling, cache workers, multitrack
playback, sequence supervision, precision seeking and the desktop smoke test.
Tests ran outside the execution sandbox because it prevents QLocalServer from
creating playback sockets. No test limits or assertions were relaxed.

Configuration, after installing Qt and running `tools/prepare_ffmpeg.py`:

```sh
cmake -S . -B /tmp/nle-macos-desktop \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/nle-macos-qt/6.10.3/macos \
  -DNLE_FFMPEG_ROOT=/tmp/nle-macos-ffmpeg \
  -DNLE_BUILD_DESKTOP=ON -DNLE_MEDIA_INTEGRATION=ON \
  -DNLE_BUILD_MCP=ON -DNLE_MCP_SDK_INTEGRATION=ON \
  -DNLE_MCP_SDK_PYTHON=/tmp/nle-macos-ci-python/bin/python \
  -DPython3_EXECUTABLE=/tmp/nle-macos-ci-python/bin/python \
  -DNLE_WARNINGS_AS_ERRORS=ON
cmake --build /tmp/nle-macos-desktop --parallel 4
ctest --test-dir /tmp/nle-macos-desktop --output-on-failure
```

Apple Silicon execution remains unverified for this fix. The build emitted an
existing linker warning about duplicate `libnle_decode.a`; compiler checks passed.
These generated-media tests do not establish physical speaker/display sync or
packaged application deployment.
