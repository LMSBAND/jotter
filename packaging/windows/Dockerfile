# Where Jotter's Windows installer is built: a MinGW cross-compiler, Qt's own
# build for Windows, the three libraries Jotter links (built here from Ubuntu's
# sources), the Python that gets shipped inside, and NSIS to make the installer.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN sed -i 's/^# deb-src/deb-src/' /etc/apt/sources.list \
    && apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config ca-certificates curl dpkg-dev \
        g++-mingw-w64-x86-64-posix \
        qtbase5-dev extra-cmake-modules \
        python3-pip nsis hunspell-en-us \
    && rm -rf /var/lib/apt/lists/*

# Qt 5.15.2 for Windows, as built by the Qt project with MinGW.
RUN pip3 install --no-cache-dir aqtinstall \
    && aqt install-qt windows desktop 5.15.2 win64_mingw81 --archives qtbase qtsvg -O /opt/qt

COPY toolchain.cmake hostqt.cmake /opt/win/

# md4c, hunspell and KDE's syntax highlighting, cross-compiled into /opt/win.
# (The highlighting library compiles its language files with a tool of its own,
# which has to be built for this machine first.)
RUN mkdir /tmp/deps && cd /tmp/deps \
    && apt-get update && apt-get source md4c hunspell ksyntax-highlighting && rm -rf /var/lib/apt/lists/* \
    && CROSS="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/win \
              -DCMAKE_TOOLCHAIN_FILE=/opt/win/toolchain.cmake -DCMAKE_PROJECT_INCLUDE=/opt/win/hostqt.cmake" \
    && cmake -S md4c-* -B md4c $CROSS -DBUILD_SHARED_LIBS=OFF && cmake --build md4c && cmake --install md4c \
    && (cd hunspell-*/src/hunspell \
        && sed 's/@HAVE_VISIBILITY@/0/' hunvisapi.h.in > hunvisapi.h \
        && mkdir -p /opt/win/bin /opt/win/lib/pkgconfig /opt/win/include/hunspell \
        && x86_64-w64-mingw32-g++-posix -O2 -s -shared -DBUILDING_LIBHUNSPELL *.cxx \
               -o /opt/win/bin/libhunspell-1.7-0.dll -Wl,--out-implib,/opt/win/lib/libhunspell-1.7.dll.a \
        && cp hunspell.h hunspell.hxx hunvisapi.h w_char.hxx atypes.hxx /opt/win/include/hunspell/) \
    && printf 'Name: hunspell\nDescription: spell checker\nVersion: 1.7.0\nLibs: -L/opt/win/lib -lhunspell-1.7\nCflags: -I/opt/win/include/hunspell\n' \
           > /opt/win/lib/pkgconfig/hunspell.pc \
    && rm -rf ksyntax-highlighting-*/po \
    && cmake -S ksyntax-highlighting-* -B ksh-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DKSYNTAXHIGHLIGHTING_USE_GUI=OFF \
    && cmake --build ksh-host --target katehighlightingindexer \
    && cmake -S ksyntax-highlighting-* -B ksh $CROSS -DECM_DIR=/usr/share/ECM/cmake -DBUILD_TESTING=OFF \
             -DKATEHIGHLIGHTINGINDEXER_EXECUTABLE=/tmp/deps/ksh-host/bin/katehighlightingindexer \
    && cmake --build ksh && cmake --install ksh \
    && cd / && rm -rf /tmp/deps

# The Python that gets shipped: python-build-standalone for Windows, with what
# plots need (this machine's pip can unpack Windows packages), and without
# what a plotting Python never uses. pip stays, so that more can be installed.
RUN url=$(curl -fsSL https://api.github.com/repos/astral-sh/python-build-standalone/releases/latest \
        | grep -o 'https://[^"]*cpython-3\.12\.[0-9]*[^"/]*x86_64-pc-windows-msvc-install_only_stripped\.tar\.gz' | head -1) \
    && test -n "$url" \
    && curl -fsSL -o /tmp/python.tar.gz "$url" && mkdir /opt/winpy && tar -xzf /tmp/python.tar.gz -C /opt/winpy && rm /tmp/python.tar.gz \
    && cd /opt/winpy/python \
    && pip3 install --no-cache-dir --target Lib/site-packages --only-binary=:all: \
            --platform win_amd64 --python-version 3.12 --implementation cp --abi cp312 numpy matplotlib \
    && rm -rf Lib/test Lib/idlelib Lib/tkinter Lib/turtledemo Lib/ensurepip Lib/lib2to3 tcl include libs \
              DLLs/tcl*.dll DLLs/tk*.dll DLLs/_tkinter.pyd DLLs/_test*.pyd DLLs/_ctypes_test.pyd \
              Lib/site-packages/bin Lib/site-packages/share \
    && find Lib/site-packages -type d \( -name tests -o -name sample_data \) -prune -exec rm -rf {} + \
    && printf '@"%%~dp0..\\python.exe" -m pip %%*\r\n' > Scripts/pip.cmd
