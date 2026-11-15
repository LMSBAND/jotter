# Cross-compiling for 64-bit Windows with MinGW (used by the build container).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Windows libraries live here: Qt's own build, and what the image built itself.
set(CMAKE_PREFIX_PATH /opt/qt/5.15.2/mingw81_64 /opt/win)
set(CMAKE_FIND_ROOT_PATH /opt/qt/5.15.2/mingw81_64 /opt/win /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
