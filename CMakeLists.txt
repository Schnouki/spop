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
pkg_check_modules(SPOTIFY REQUIRED libspotify>=0.0.3)

# spop daemon
set(SPOPD
  src/appkey.c
  src/main.c
  src/plugin.c
  src/session.c
)
add_executable(spopd ${SPOPD})

target_link_libraries(spopd ${SPOTIFY_LIBRARIES} dl)

# OSS plugin
set(OSS
  src/oss.c
)
add_library(spop_oss MODULE ${OSS})

# common to all source files
set(SRC
  ${SPOPD}
  ${OSS}
)
set_source_files_properties(${SRC}
  COMPILE_FLAGS "-O2 -Wall -Werror"
)
