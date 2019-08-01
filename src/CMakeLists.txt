###############################################################################
# src/CMakeLists.txt
#
# CMake file for sqlplot-tools utility set.
#
###############################################################################
# Copyright (C) 2013-2015 Timo Bingmann <tb@panthema.net>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, see <http://www.gnu.org/licenses/>.
###############################################################################

set(SQL_SOURCES "")

if(PostgreSQL_FOUND)
  set(SQL_SOURCES ${SQL_SOURCES} pgsql.cpp)
endif()

if(MYSQL_FOUND)
  set(SQL_SOURCES ${SQL_SOURCES} mysql.cpp)
endif()

add_executable(sqlplot-tools
  main.cpp
  latex.cpp
  gnuplot.cpp
  common.cpp
  sql.cpp
  sqlite.cpp
  sqlite-functions.cpp
  ${SQL_SOURCES}
  importdata.cpp
  fieldset.cpp
  reformat.cpp
  )

target_link_libraries(sqlplot-tools ${SQL_LIBRARIES} ${Boost_LIBRARIES})

install(TARGETS sqlplot-tools RUNTIME DESTINATION ${INSTALL_BIN_DIR})

################################################################################
