###############################################################################
# CMakeLists.txt
#
# Build script for sqlplot-tools utility set.
#
###############################################################################
# Copyright (C) 2013-2014 Timo Bingmann <tb@panthema.net>
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

cmake_minimum_required(VERSION 2.8)

project(sqlplot-tools)

# prohibit in-source builds
if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
  message(SEND_ERROR "In-source builds are not allowed.")
endif()

# default to Debug building for single-config generators
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message("Defaulting CMAKE_BUILD_TYPE to Debug")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# option to update all tests with new output
option(UPDATE_TESTS "Update test output with program output" OFF)

# option to build examples
option(BUILD_EXAMPLES "Build example programs (for build testing)" OFF)

# option to change the database system to run tests on
set(TEST_DATABASE "Sqlite"
  CACHE STRING "Select the database system to run tests on.")

# enable warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall")

# enable use of "make test"
enable_testing()

# Find SQL libraries
set(SQL_LIBRARIES "")

# Find PostgreSQL client library

set(PostgreSQL_ADDITIONAL_VERSIONS "9.3" "9.2")

find_package(PostgreSQL)

if(PostgreSQL_FOUND)
  set(SQL_LIBRARIES ${SQL_LIBRARIES} ${PostgreSQL_LIBRARIES})
  add_definitions(-DHAVE_POSTGRESQL=1)
  message(STATUS "Found PostgreSQL: ${PostgreSQL_LIBRARIES}")
  include_directories(${PostgreSQL_INCLUDE_DIRS})
endif()

# Find MySQL client library
find_path(MYSQL_INCLUDE_DIRS mysql.h
  PATHS /usr/include/mysql /usr/local/include/mysql)

find_library(MYSQL_LIBRARIES NAMES mysqlclient
  PATHS /usr/lib/mysql /usr/local/lib/mysql)

if(MYSQL_INCLUDE_DIRS AND MYSQL_LIBRARIES)
  set(MYSQL_FOUND TRUE)
  add_definitions(-DHAVE_MYSQL=1)
  message(STATUS "Found MySQL: ${MYSQL_LIBRARIES}")
  include_directories(${MYSQL_INCLUDE_DIRS})
  set(SQL_LIBRARIES ${SQL_LIBRARIES} ${MYSQL_LIBRARIES})
else()
  set(MYSQL_FOUND FALSE)
  message(STATUS "Could NOT find MySQL library")
endif()

# Find Sqlite3 library
find_path(SQLITE3_INCLUDE_DIRS sqlite3.h
  PATHS /usr/include /usr/local/include)

find_library(SQLITE3_LIBRARIES NAMES sqlite3
  PATHS /usr/lib /usr/local/lib)
include_directories(${SQLITE3_INCLUDE_DIRS})

if(SQLITE3_INCLUDE_DIRS AND SQLITE3_LIBRARIES)
  set(SQLITE3_FOUND TRUE)
  add_definitions(-DHAVE_SQLITE3=1)
  message(STATUS "Found SQLite3: ${SQLITE3_LIBRARIES}")
  include_directories(${SQLITE3_INCLUDE_DIRS})
  set(SQL_LIBRARIES ${SQL_LIBRARIES} ${SQLITE3_LIBRARIES})
else()
  set(SQLITE3_FOUND FALSE)
  message(SEND_ERROR "Could NOT find SQLite3 library. It is required!")
endif()

# Use Boost.Regex
find_package(Boost 1.46.0 REQUIRED COMPONENTS regex)
include_directories(${Boost_INCLUDE_DIRS})

# descend into source
add_subdirectory(src)

# descend into testsuite
add_subdirectory(tests)

# build examples (for testing)
if(BUILD_EXAMPLES)
  add_subdirectory(examples/sorting-speed)
  add_subdirectory(examples/stats_writer)
endif()
