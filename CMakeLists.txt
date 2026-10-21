cmake_minimum_required(VERSION 3.20)
project(kennel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(KENNEL_ALSA "Volume and audio output control through ALSA" ON)
option(KENNEL_STATIC_CXX "Link libstdc++ and libgcc statically" OFF)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(Threads REQUIRED)

find_package(ZLIB REQUIRED)

# Code shared by kennel and kennel-read.
add_library(kennelcore STATIC
    src/zip.cpp
    src/tokenizer.cpp
    src/cover.cpp
    src/anim.cpp
    src/util.cpp
    src/ini.cpp
    src/paths.cpp
    src/platform.cpp
    src/settings.cpp
    src/image.cpp
    src/gfx.cpp
    src/input.cpp
    src/menu.cpp
)
target_include_directories(kennelcore PUBLIC src third_party)
target_compile_definitions(kennelcore PUBLIC KENNEL_VERSION="${PROJECT_VERSION}")
target_compile_options(kennelcore PUBLIC -Wall -Wextra -Wno-missing-field-initializers)
target_link_libraries(kennelcore PUBLIC SDL3::SDL3 SDL3_ttf::SDL3_ttf Threads::Threads ZLIB::ZLIB)

add_executable(kennel
    src/main.cpp
    src/app.cpp
    src/catalog.cpp
    src/stats.cpp
    src/art.cpp
    src/system.cpp
    src/procs.cpp
    src/widgets.cpp
    src/home.cpp
    src/details.cpp
    src/panels.cpp
    src/menus.cpp
    src/expert.cpp
    src/statsview.cpp
)
target_link_libraries(kennel PRIVATE kennelcore)

file(GLOB READ_SOURCES CONFIGURE_DEPENDS src/read/*.cpp)
add_executable(kennel-read ${READ_SOURCES})
target_include_directories(kennel-read PRIVATE src/read)
target_link_libraries(kennel-read PRIVATE kennelcore)

if(KENNEL_ALSA)
    find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h REQUIRED)
    find_library(ALSA_LIBRARY asound REQUIRED)
    target_include_directories(kennel PRIVATE ${ALSA_INCLUDE_DIR})
    target_link_libraries(kennel PRIVATE ${ALSA_LIBRARY})
    target_compile_definitions(kennel PRIVATE KENNEL_ALSA=1)
endif()

if(KENNEL_STATIC_CXX)
    target_link_options(kennel PRIVATE -static-libstdc++ -static-libgcc)
    target_link_options(kennel-read PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Run from the build directory with the repository's data.
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets SYMBOLIC)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/platforms ${CMAKE_BINARY_DIR}/platforms SYMBOLIC)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/tools/kennel-run ${CMAKE_BINARY_DIR}/kennel-run SYMBOLIC)

install(TARGETS kennel kennel-read DESTINATION .)
install(PROGRAMS tools/kennel-run DESTINATION .)
install(DIRECTORY assets/fonts DESTINATION assets)
install(DIRECTORY assets/icons/20 assets/icons/32 DESTINATION assets/icons)
install(FILES assets/icons/LICENSE-lucide.txt DESTINATION assets/icons)
install(DIRECTORY platforms DESTINATION .)
