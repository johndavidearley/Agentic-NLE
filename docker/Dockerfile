# Ubuntu 24.04 matches the ubuntu-24.04 CI runner for the headless jobs:
# GCC/Clang Debug and Release, clang-format 19, sanitizers, and the FFmpeg corpus.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        clang-format-19 \
        cmake \
        ffmpeg \
        libegl1-mesa-dev \
        libfontconfig1 \
        libgl1-mesa-dev \
        libpulse0 \
        libxcb-cursor0 \
        libxkbcommon-dev \
        python3 \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

COPY test-ubuntu.sh /usr/local/bin/test-ubuntu.sh
RUN sed -i 's/\r$//' /usr/local/bin/test-ubuntu.sh \
    && chmod +x /usr/local/bin/test-ubuntu.sh

WORKDIR /src
ENTRYPOINT ["/usr/local/bin/test-ubuntu.sh"]
