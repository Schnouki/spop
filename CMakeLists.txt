cmake_minimum_required(VERSION 2.8)

project(spop C)

find_package(PkgConfig)
pkg_check_modules(SPOTIFY REQUIRED libspotify>=0.0.3)

set(SRC
  src/appkey.c
  src/main.c
  src/session.c
)
set_source_files_properties(${SRC}
  COMPILE_FLAGS "-O2 -Wall -Werror"
)
add_executable(spop ${SRC})

target_link_libraries(spop ${SPOTIFY_LIBRARIES})
