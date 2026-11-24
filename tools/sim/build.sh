#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

mkdir -p .simbin

g++ -std=c++17 -O2 -Wall -Wextra \
  -Ilib/cpr_core/src -Ilib/cpr_sim/src \
  lib/cpr_core/src/*.cpp \
  lib/cpr_sim/src/*.cpp \
  tools/sim/main.cpp \
  -o .simbin/cpr_sim
