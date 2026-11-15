#!/bin/sh
# Build Jotter and install it for the current user (~/.local).
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$HOME/.local"
# The menu entry should work even where ~/.local/bin is not on the session's PATH.
sed -i "s|^Exec=jotter|Exec=$HOME/.local/bin/jotter|" "$HOME/.local/share/applications/jotter.desktop"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
echo "Installed: $HOME/.local/bin/jotter"
