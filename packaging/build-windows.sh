#!/bin/sh
# Runs inside the build container (started by windows.sh): cross-compile, lay
# out a folder with everything Jotter needs on Windows, pack it into an installer.
set -e
SRC=/src
WORK=/work
APP=$WORK/Jotter
QT=/opt/qt/5.15.2/mingw81_64
GCC=/usr/lib/gcc/x86_64-w64-mingw32/10-posix

export PKG_CONFIG_LIBDIR=/opt/win/lib/pkgconfig
cmake -S $SRC -B $WORK/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/win/toolchain.cmake -DCMAKE_PROJECT_INCLUDE=/opt/win/hostqt.cmake
cmake --build $WORK/build

rm -rf "$APP"
mkdir -p $APP/platforms $APP/styles $APP/imageformats $APP/iconengines $APP/share/hunspell
cp $WORK/build/jotter.exe $APP/

# Qt and the other libraries, the compiler's runtime, and the Qt plugins in use.
cp $QT/bin/Qt5Core.dll $QT/bin/Qt5Gui.dll $QT/bin/Qt5Widgets.dll $QT/bin/Qt5Svg.dll $QT/bin/Qt5Network.dll $APP/
cp /opt/win/bin/libKF5SyntaxHighlighting.dll /opt/win/bin/libhunspell-1.7-0.dll $APP/
cp $GCC/libgcc_s_seh-1.dll $GCC/libstdc++-6.dll /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll $APP/
cp $QT/plugins/platforms/qwindows.dll $APP/platforms/
cp $QT/plugins/styles/qwindowsvistastyle.dll $APP/styles/
cp $QT/plugins/imageformats/qgif.dll $QT/plugins/imageformats/qico.dll $QT/plugins/imageformats/qjpeg.dll \
   $QT/plugins/imageformats/qsvg.dll $APP/imageformats/
cp $QT/plugins/iconengines/qsvgicon.dll $APP/iconengines/
x86_64-w64-mingw32-strip $APP/jotter.exe $APP/libKF5SyntaxHighlighting.dll \
    $APP/libgcc_s_seh-1.dll $APP/libstdc++-6.dll $APP/libwinpthread-1.dll

# The text faces are inside jotter.exe; their licences go along with it.
for licence in $SRC/data/fonts/*/OFL.txt $SRC/data/fonts/*/LICENSE.md; do
    mkdir -p "$APP/share/licenses/fonts/$(basename "$(dirname "$licence")")"
    cp "$licence" "$APP/share/licenses/fonts/$(basename "$(dirname "$licence")")/"
done

# What a bare machine does not have: a Python that can plot, and a dictionary.
cp -a /opt/winpy/python $APP/python
cp /usr/share/hunspell/en_US.aff /usr/share/hunspell/en_US.dic $APP/share/hunspell/

VERSION=$(sed -n 's/^project(jotter VERSION \([0-9.]*\).*/\1/p' $SRC/CMakeLists.txt)
rm -f /out/Jotter-Setup-x86_64.exe
makensis -V2 -DVERSION="$VERSION" -DAPP="$APP" -DICON=$SRC/packaging/windows/jotter.ico \
    -DSIZE_KB="$(du -sk "$APP" | cut -f1)" -DOUT=/out/Jotter-Setup-x86_64.exe $SRC/packaging/windows/installer.nsi
