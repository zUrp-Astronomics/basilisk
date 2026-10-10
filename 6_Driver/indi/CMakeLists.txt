# SOURCE: INDI focuser driver for the Basilisk E-mount lens controller, over the upstream pinefeat_cef
# AUTHOR: engineer
# DATE: 2026-10-02
# STATUS: active — built and linked by the `indi` job of .gitea/workflows/ci.yml
cmake_minimum_required(VERSION 3.13)
project(indi_basilisk_focus CXX)

#   mkdir build && cd build && cmake .. && make && sudo make install
# Requires libindi-dev (1.9.9 or 2.x).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# indi-3rdparty ships FindINDI.cmake ; a plain libindi-dev only ships pkg-config
find_package(INDI QUIET)
if(INDI_FOUND)
    include_directories(${INDI_INCLUDE_DIR})
    set(BASILISK_INDI_LIBS ${INDI_LIBRARIES} ${INDI_DRIVER_LIBRARIES})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(INDI REQUIRED libindi)
    include_directories(${INDI_INCLUDE_DIRS})
    link_directories(${INDI_LIBRARY_DIRS})
    set(BASILISK_INDI_LIBS ${INDI_LIBRARIES} indidriver)
endif()

add_executable(indi_basilisk_focus pinefeat_cef.cpp)
target_link_libraries(indi_basilisk_focus ${BASILISK_INDI_LIBS})

install(TARGETS indi_basilisk_focus RUNTIME DESTINATION bin)
install(FILES indi_basilisk_focus.xml DESTINATION ${CMAKE_INSTALL_PREFIX}/share/indi)
