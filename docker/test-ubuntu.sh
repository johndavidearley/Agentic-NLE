#!/bin/bash
# Run the headless Ubuntu 24.04 checks inside the container.
# The source tree is mounted at /src. Each suite keeps its build directory under /build.
set -euo pipefail

src=/src
jobs="$(nproc)"

usage() {
    cat <<'EOF'
Usage: test-ubuntu.sh [core|debug|release|clang|format|sanitizers|media|all]

  core        GCC and Clang, Debug and Release (default; matches the Ubuntu CI build job)
  debug       GCC Debug
  release     GCC Release
  clang       Clang Debug and Release
  format      clang-format 19 check
  sanitizers  Clang address and undefined-behavior sanitizers, Debug
  media       Release build with the generated FFmpeg media corpus
  desktop     Ubuntu desktop CI job: Qt 6.10.3, FFmpeg corpus, and MCP export
  all         core, format, sanitizers, and media
EOF
}

run_config() {
    local name="$1"
    local build_type="$2"
    shift 2
    local dir="/build/${name}"
    echo "==> ${name} (${build_type})"
    cmake -S "${src}" -B "${dir}" -DCMAKE_BUILD_TYPE="${build_type}" "$@"
    cmake --build "${dir}" --parallel "${jobs}"
    ctest --test-dir "${dir}" --output-on-failure
}

core() {
    run_config gcc-debug Debug
    run_config gcc-release Release
    run_config clang-debug Debug -DCMAKE_CXX_COMPILER=clang++
    run_config clang-release Release -DCMAKE_CXX_COMPILER=clang++
}

format_check() {
    echo "==> format (clang-format 19)"
    cmake -S "${src}" -B /build/format -DCLANG_FORMAT=clang-format-19
    cmake --build /build/format --target format-check --parallel "${jobs}"
}

sanitizers() {
    run_config sanitizers Debug -DCMAKE_CXX_COMPILER=clang++ -DNLE_SANITIZERS=ON
}

media() {
    run_config media Release -DNLE_MEDIA_INTEGRATION=ON
}

desktop() {
    local venv="/build/venv"
    local qt_root="/build/qt"
    if [ ! -x "${venv}/bin/python" ]; then
        python3 -m venv "${venv}"
        "${venv}/bin/pip" install --disable-pip-version-check aqtinstall==3.3.0 'mcp==2.2.0'
    fi
    if [ ! -e "${qt_root}/6.10.3/gcc_64/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        # aqt writes aqtinstall.log in the working directory. Keep it out of the source tree.
        (cd /build && "${venv}/bin/python" -m aqt install-qt linux desktop 6.10.3 linux_gcc_64 \
            --outputdir "${qt_root}" --modules qtmultimedia --archives qtbase)
        (cd /build && "${venv}/bin/python" -m aqt install-qt linux desktop 6.10.3 linux_gcc_64 \
            --outputdir "${qt_root}" --archives icu qtdeclarative)
    fi
    local config
    config="$(find "${qt_root}/6.10.3" -path '*/lib/cmake/Qt6/Qt6Config.cmake' -print -quit)"
    if [ -z "${config}" ]; then
        echo "Qt 6.10.3 configuration was not installed." >&2
        exit 1
    fi
    local prefix
    prefix="$(dirname "$(dirname "$(dirname "$(dirname "${config}")")")")"
    local plugin="${prefix}/plugins/multimedia/libffmpegmediaplugin.so"
    if [ ! -f "${plugin}" ]; then
        echo "Missing Qt FFmpeg plugin: ${plugin}" >&2
        exit 1
    fi
    local dependencies
    dependencies="$(ldd "${plugin}")"
    printf '%s\n' "${dependencies}"
    if printf '%s\n' "${dependencies}" | grep -q 'not found'; then
        exit 1
    fi
    "${venv}/bin/python" "${src}/tools/prepare_ffmpeg.py" "${prefix}" /build/ffmpeg
    "${venv}/bin/python" "${src}/tools/prepare_ffmpeg_test.py" /build/ffmpeg/ffmpeg-7.1.3.tar.xz
    run_config desktop Release \
        -DNLE_BUILD_DESKTOP=ON \
        -DNLE_MEDIA_INTEGRATION=ON \
        -DNLE_BUILD_MCP=ON \
        -DNLE_MCP_SDK_INTEGRATION=ON \
        -DNLE_MCP_SDK_PYTHON="${venv}/bin/python" \
        -DNLE_FFMPEG_ROOT=/build/ffmpeg \
        -DCMAKE_PREFIX_PATH="${prefix}"
}

if [ "$#" -gt 1 ]; then
    echo "Expected one suite name." >&2
    usage >&2
    exit 2
fi

suite="${1:-core}"
case "${suite}" in
    core) core ;;
    debug) run_config gcc-debug Debug ;;
    release) run_config gcc-release Release ;;
    clang)
        run_config clang-debug Debug -DCMAKE_CXX_COMPILER=clang++
        run_config clang-release Release -DCMAKE_CXX_COMPILER=clang++
        ;;
    format) format_check ;;
    sanitizers) sanitizers ;;
    media) media ;;
    desktop) desktop ;;
    all)
        core
        format_check
        sanitizers
        media
        ;;
    -h | --help | help)
        usage
        ;;
    *)
        echo "Unknown suite: ${suite}" >&2
        usage >&2
        exit 2
        ;;
esac
