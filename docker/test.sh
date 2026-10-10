#!/bin/sh
# Run an Ubuntu 24.04 test suite from macOS or Linux. Pass one suite name, or none for core.
set -eu

if ! command -v docker >/dev/null 2>&1; then
    echo "Install Docker Desktop and start it before running Ubuntu tests." >&2
    exit 1
fi

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "${root}"
exec docker compose run --rm ubuntu "$@"
