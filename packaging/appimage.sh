#!/bin/sh
# Build dist/Jotter-x86_64.AppImage: one file holding Jotter and what it needs
# to run (Qt, the PDF engine, Python with numpy and matplotlib, a dictionary).
# Needs Docker; the build happens in an Ubuntu 22.04 container so that the
# result runs there and on anything newer.
set -e
cd "$(dirname "$0")/.."
docker build -t jotter-build packaging
mkdir -p dist build/appimage
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$PWD":/src:ro -v "$PWD/build/appimage":/work -v "$PWD/dist":/out \
    jotter-build sh /src/packaging/build-appimage.sh
ls -lh dist/Jotter-x86_64.AppImage
