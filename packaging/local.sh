#!/bin/sh
# Build Jotter in the Linux build container and put it in ~/.local/bin: for
# trying a change on this machine, which needs Docker but none of the Qt headers.
#   packaging/local.sh              build and install
#   packaging/local.sh test         build and run the tests
#   packaging/local.sh test kernel  ...only those whose name contains "kernel"
set -e
cd "$(dirname "$0")/.."
docker build -q -t jotter-build packaging >/dev/null
mkdir -p build/appimage
run() {
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$PWD":/src:ro -v "$PWD/build/appimage":/work jotter-build sh -c "$1"
}
run 'cmake -S /src -B /work/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr >/dev/null \
     && cmake --build /work/build -j"$(nproc)" --target jotter jotter-tests'
if [ "$1" = test ]; then
    run "cd /tmp && PATH=/opt/python/bin:\$PATH QT_QPA_PLATFORM=offscreen /work/build/jotter-tests $2"
else
    install -m755 build/appimage/build/jotter "$HOME/.local/bin/jotter.new"
    mv "$HOME/.local/bin/jotter.new" "$HOME/.local/bin/jotter"     # (a Jotter that is open keeps running the old one)
    echo "Installed: $HOME/.local/bin/jotter"
fi
