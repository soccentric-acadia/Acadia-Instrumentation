#!/bin/sh
# Build/test dependencies for a clean Ubuntu 24.04 host (INS-020). Shared by
# .github/workflows/ci.yml and the local container reproduction, so the CI
# package set is exercised exactly as written.
set -eu
export DEBIAN_FRONTEND=noninteractive
SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"
$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config python3 python3-yaml git ca-certificates \
    protobuf-compiler protobuf-compiler-grpc libprotobuf-dev libgrpc++-dev \
    libyaml-cpp-dev libgtest-dev libgmock-dev libssl-dev \
    clang-format-18
