###############################################################################
# tests/latex/CMakeLists.txt
#
# Runs sqlplot-tools against some test files and verify the output.
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

# find all .tex files in current directory
file(GLOB tex_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/" "*.tex")

# test processed output against saved output
foreach(infile ${tex_files})
  # replace .tex -> .out
  string(REGEX REPLACE "\\.tex$" ".out" outfile ${infile})
  # basename for test target name
  get_filename_component(basename ${infile} NAME)
  # write test case
  add_test(NAME latex_${basename}
    COMMAND ${CMAKE_BINARY_DIR}/src/sqlplot-tools
      ${TEST_OPTIONS} ${infile} -o ${outfile} -W  ${CMAKE_CURRENT_SOURCE_DIR}
    )
endforeach()
