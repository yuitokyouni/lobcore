#!/usr/bin/env bash
# Idempotent repository bootstrap for the lobcore development environment.
# Runs after the repository is checked out. Safe to run repeatedly.
set -euo pipefail

cd "$(dirname "$0")/.."

# --- System toolchain -------------------------------------------------------
# The default image ships cmake/clang/gcc/python3 but is missing the C++
# standard-library dev files, the clang sanitizer runtime (Debug enables
# ASan/UBSan by default), the Ninja generator that CI uses, and the Python
# venv/dev headers needed to build the pybind11 module.
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  ninja-build \
  g++ \
  libstdc++-14-dev \
  libclang-rt-18-dev \
  python3-venv \
  python3-dev \
  python3-pip

# --- C++ Debug build (Catch2 spec suite) ------------------------------------
# Priming the Debug tree fetches Catch2 and confirms the sanitized build links.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# --- Python bindings --------------------------------------------------------
# Build/install the pybind11 module into an isolated venv (PEP 668 safe).
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e "./python[test]"
