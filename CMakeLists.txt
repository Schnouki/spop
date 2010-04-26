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
pkg_check_modules(SPOTIFY libspotify>=0.0.3)
if(NOT SPOTIFY_FOUND)
  pkg_check_modules(SPOTIFY REQUIRED libopenspotify)
  add_definitions(-DOPENSPOTIFY)
endif(NOT SPOTIFY_FOUND)

# Check for required libraries
pkg_check_modules(CONFIG REQUIRED libconfig)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)

# spop daemon
set(SPOPD
  src/appkey.c
  src/config.c
  src/main.c
  src/playlist.c
  src/plugin.c
  src/session.c
)
add_executable(spopd ${SPOPD})

string(REPLACE ";" " " SPOPD_CFLAGS "${SPOTIFY_CFLAGS} ${CONFIG_CFLAGS} ${GLIB2_CFLAGS}")
set_target_properties(spopd PROPERTIES
  COMPILE_FLAGS ${SPOPD_CFLAGS}
)
target_link_libraries(spopd dl ${SPOTIFY_LIBRARIES} ${CONFIG_LIBRARIES} ${GLIB2_LIBRARIES})

# OSS audio plugin
set(AUDIO_OSS
  src/oss.c
)
add_library(spop_audio_oss MODULE ${AUDIO_OSS})

# MPD interface plugin
set(IF_MPD
  src/interface-mpd.c
)
add_library(spop_interface_mpd MODULE ${IF_MPD})

# Simple interface plugin
#set(IF_SIMPLE
#  src/interface-simple.c
#)
#add_library(spop_interface_simple MODULE ${IF_SIMPLE})

# common to all source files
set(SRC
  ${SPOPD}
  ${OSS}
)
set_source_files_properties(${SRC}
  COMPILE_FLAGS "-O2 -Wall -Werror"
)
