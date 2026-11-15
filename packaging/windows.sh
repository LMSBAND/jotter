#!/bin/sh
# Build dist/Jotter-Setup-x86_64.exe: an installer for 64-bit Windows holding
# Jotter and what it needs to run (Qt, Python with numpy and matplotlib, a
# dictionary). Needs Docker; Jotter is cross-compiled with MinGW in an Ubuntu
# container, so no Windows machine is involved.
set -e
cd "$(dirname "$0")/.."
docker build -t jotter-build-windows packaging/windows
mkdir -p dist build/windows
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD":/src:ro -v "$PWD/build/windows":/work -v "$PWD/dist":/out \
    jotter-build-windows sh /src/packaging/build-windows.sh
ls -lh dist/Jotter-Setup-x86_64.exe
