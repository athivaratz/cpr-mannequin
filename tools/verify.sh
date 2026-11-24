#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

pio test -e native
pio run -e esp32dev
pio run -e nano
