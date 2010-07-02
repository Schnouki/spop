# This file is part of spop.
#
# spop is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# spop is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# spop. If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 2.8)

project(spop C)

find_package(PkgConfig)

# Check for libspotify or libopenspotify
pkg_check_modules(SPOTIFY libspotify>=0.0.4)
if(NOT SPOTIFY_FOUND)
  pkg_check_modules(SPOTIFY REQUIRED libopenspotify)
  add_definitions(-DOPENSPOTIFY)
endif(NOT SPOTIFY_FOUND)
 string(REPLACE ";" " " SPOTIFY_CFLAGS "${SPOTIFY_CFLAGS}")

# Check for required libraries
pkg_check_modules(CONFIG REQUIRED libconfig)
string(REPLACE ";" " " CONFIG_CFLAGS "${CONFIG_CFLAGS}")
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
string(REPLACE ";" " " GLIB2_CFLAGS "${GLIB2_CFLAGS}")
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
string(REPLACE ";" " " GTHREAD2_CFLAGS "${GTHREAD2_CFLAGS}")

# spop daemon
set(SPOPD
  src/appkey.c
  src/commands.c
  src/config.c
  src/interface.c
  src/main.c
  src/plugin.c
  src/queue.c
  src/spotify.c
)
add_executable(spopd ${SPOPD})

set_target_properties(spopd PROPERTIES
  COMPILE_FLAGS "${SPOTIFY_CFLAGS} ${CONFIG_CFLAGS} ${GLIB2_CFLAGS} ${GTHREAD2_CFLAGS}"
)
target_link_libraries(spopd dl ${SPOTIFY_LIBRARIES} ${CONFIG_LIBRARIES} ${GLIB2_LIBRARIES} ${GTHREAD2_LIBRARIES})

# OSS audio plugin
set(AUDIO_OSS
  src/oss.c
)
add_library(spop_audio_oss MODULE ${AUDIO_OSS})

# common to all source files
set(SRC
  ${SPOPD}
  ${AUDIO_OSS}
)
set_source_files_properties(${SRC}
  COMPILE_FLAGS "-O2 -Wall -Werror"
)
