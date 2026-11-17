#!/bin/sh
# Runs inside the build container (started by appimage.sh): compile, lay out an
# AppDir with everything Jotter needs, pack it into one file.
set -e
SRC=/src
WORK=/work
A=$WORK/AppDir

cmake -S $SRC -B $WORK/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build $WORK/build -j"$(nproc)"
rm -rf "$A"
DESTDIR=$A cmake --install $WORK/build

# What a bare machine may not have: a Python that can plot, and a dictionary.
cp -a /opt/python $A/usr/python
mkdir -p $A/usr/share/hunspell
cp /usr/share/hunspell/en_US.aff /usr/share/hunspell/en_US.dic $A/usr/share/hunspell/

# Qt, its plugins (X11 and Wayland, SVG icons) and the PDF engine.
cd $WORK
rm -f Jotter*.AppImage
export QMAKE=/usr/bin/qmake
export EXTRA_QT_MODULES="svg;waylandcompositor"
export EXTRA_QT_PLUGINS="$EXTRA_QT_MODULES"     # the same setting under its older name
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export VERSION=$(sed -n 's/^project(jotter VERSION \([0-9.]*\).*/\1/p' $SRC/CMakeLists.txt)
/opt/tools/linuxdeploy --appdir $A \
    -e $A/usr/bin/jotter -e $A/usr/bin/jotter-pdf \
    -d $A/usr/share/applications/jotter.desktop \
    -i $A/usr/share/icons/hicolor/scalable/apps/jotter.svg \
    --plugin qt --output appimage
mv Jotter*.AppImage /out/Jotter-x86_64.AppImage

# The same thing as a folder to unpack, for where an AppImage will not start
# (no FUSE) or is not wanted: Jotter/AppRun starts it.
tar -C $WORK --transform 's,^AppDir,Jotter,' -czf /out/Jotter-x86_64.tar.gz AppDir
