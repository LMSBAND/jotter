# Where Jotter's AppImage is built: an older Ubuntu (so the result also runs on
# everything newer), the build dependencies, the packaging tools, and the Python
# that gets shipped inside.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config file ca-certificates curl \
        qtbase5-dev qtwebengine5-dev libkf5syntaxhighlighting-dev libmd4c-dev libhunspell-dev \
        libqt5svg5 qtwayland5 hunspell-en-us \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/tools && cd /opt/tools \
    && curl -fsSL -o linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
    && curl -fsSL -o linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
    && chmod +x linuxdeploy linuxdeploy-plugin-qt

# A relocatable Python (python-build-standalone) with what plots need, and
# without what a read-only, plotting-only Python never uses.
RUN url=$(curl -fsSL https://api.github.com/repos/astral-sh/python-build-standalone/releases/latest \
        | grep -o 'https://[^"]*cpython-3\.12\.[0-9]*[^"/]*x86_64-unknown-linux-gnu-install_only_stripped\.tar\.gz' | head -1) \
    && test -n "$url" \
    && curl -fsSL -o /tmp/python.tar.gz "$url" && tar -xzf /tmp/python.tar.gz -C /opt && rm /tmp/python.tar.gz \
    && /opt/python/bin/python3 -m pip install --no-cache-dir numpy matplotlib \
    && cd /opt/python \
    && rm -rf lib/python3.12/test lib/python3.12/idlelib lib/python3.12/tkinter lib/python3.12/turtledemo \
              lib/python3.12/ensurepip lib/python3.12/lib2to3 lib/tcl* lib/tk* lib/itcl* lib/thread* \
              lib/libtcl* lib/libtk* share/man include \
              lib/python3.12/site-packages/pip lib/python3.12/site-packages/pip-* bin/pip* \
    && find lib/python3.12/site-packages -type d \( -name tests -o -name sample_data \) -prune -exec rm -rf {} + \
    && (bin/python3 -m compileall -q lib/python3.12 || true)

# linuxdeploy is itself an AppImage; containers have no FUSE to mount it with.
ENV APPIMAGE_EXTRACT_AND_RUN=1
