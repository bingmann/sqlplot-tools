###############################################################################
# src/CMakeLists.txt
#
# CMake file for sqlplot-tools utility set.
#
###############################################################################
# Copyright (C) 2013 Timo Bingmann <tb@panthema.net>
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

include_directories(${PostgreSQL_INCLUDE_DIRS})

add_executable(sp-importdata
  sp-importdata.cpp
  common.cpp
  importdata.cpp
  fieldset.cpp
  )
target_link_libraries(sp-importdata ${PostgreSQL_LIBRARIES} ${Boost_LIBRARIES})

add_executable(sp-latex
  sp-latex.cpp
  common.cpp
  importdata.cpp
  fieldset.cpp
  )
target_link_libraries(sp-latex ${PostgreSQL_LIBRARIES} ${Boost_LIBRARIES})
