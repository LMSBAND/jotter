# Qt's Windows package points CMake at moc.exe and friends, which cannot run
# here. Name the build machine's own tools first and Qt's files leave them be.
foreach(tool moc rcc uic qmake)
  if(NOT TARGET Qt5::${tool})
    add_executable(Qt5::${tool} IMPORTED)
    set_target_properties(Qt5::${tool} PROPERTIES IMPORTED_LOCATION /usr/lib/qt5/bin/${tool})
  endif()
endforeach()
