cmake_minimum_required(VERSION 3.16)
project(jotter VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 5.14 REQUIRED COMPONENTS Widgets)
find_package(KF5SyntaxHighlighting REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MD4C REQUIRED IMPORTED_TARGET md4c)
pkg_check_modules(HUNSPELL REQUIRED IMPORTED_TARGET hunspell)
find_package(Qt5 QUIET COMPONENTS WebEngineWidgets Test)

# Everything except main() lives in a library so the tests can link it too.
add_library(jottercore STATIC
  src/model.cpp
  src/theme.cpp
  src/assets.cpp
  src/markdown.cpp
  src/kernel.cpp
  src/mathtex.cpp
  src/objects.cpp
  src/render.cpp
  src/editor.cpp
  src/editor_commands.cpp
  src/storyboard.cpp
  src/history.cpp
  src/exporter.cpp
  src/mainwindow.cpp
  data/resources.qrc
)
target_include_directories(jottercore PUBLIC src)
target_compile_options(jottercore PRIVATE -Wall -Wextra)
target_compile_definitions(jottercore PUBLIC JOTTER_VERSION="${PROJECT_VERSION}" QT_NO_CAST_FROM_BYTEARRAY)
target_link_libraries(jottercore PUBLIC Qt5::Widgets KF5::SyntaxHighlighting PkgConfig::MD4C PkgConfig::HUNSPELL)
if(WIN32)
  target_compile_definitions(jottercore PUBLIC _WIN32_WINNT=0x0601)   # the Windows API as of Windows 7
endif()

add_executable(jotter src/main.cpp)
target_link_libraries(jotter PRIVATE jottercore)
if(WIN32)
  # A program with an icon and no console window.
  target_sources(jotter PRIVATE packaging/windows/jotter.rc)
  target_include_directories(jotter PRIVATE packaging/windows)
  set_target_properties(jotter PROPERTIES WIN32_EXECUTABLE ON)
endif()

# PDF export runs in its own small process so the editor never loads a browser engine.
if(Qt5WebEngineWidgets_FOUND)
  add_executable(jotter-pdf src/pdfhelper.cpp)
  target_link_libraries(jotter-pdf PRIVATE Qt5::WebEngineWidgets)
  install(TARGETS jotter-pdf RUNTIME DESTINATION bin)
endif()

if(Qt5Test_FOUND)
  add_executable(jotter-tests tests/tests.cpp)
  target_link_libraries(jotter-tests PRIVATE jottercore Qt5::Test)
endif()

install(TARGETS jotter RUNTIME DESTINATION bin)
if(UNIX)
  install(FILES data/jotter.desktop DESTINATION share/applications)
  install(FILES data/jotter.svg DESTINATION share/icons/hicolor/scalable/apps)
  # The text faces are inside the program; their licences go along with it.
  install(DIRECTORY data/fonts/ DESTINATION share/licenses/jotter/fonts
          FILES_MATCHING PATTERN "OFL.txt" PATTERN "LICENSE.md")
endif()
