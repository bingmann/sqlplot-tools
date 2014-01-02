/******************************************************************************
 * src/common.h
 *
 * Common global variables across all programs.
 *
 ******************************************************************************
 * Copyright (C) 2013 Timo Bingmann <tb@panthema.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#ifndef COMMON_HEADER
#define COMMON_HEADER

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <libpq-fe.h>

//! verbosity, common global option.
extern int gopt_verbose;

//! check processed output matches the output file
extern bool gopt_check_output;

//! global PostgreSQL connection handle
extern PGconn* g_pg;

//! conditional debug output
#define OUTC(dbg,X)   do { if (dbg) { std::cerr << X; } } while(0)

//! debug output to std::cerr without newline
#define OUTX(X)       OUTC(true, X)

//! debug output to std::cerr
#define OUT(X)        OUTX(X << std::endl)

//! format output and throw std::runtime_error
#define OUT_THROW(X)  do { std::ostringstream os; os << X; throw(std::runtime_error(os.str())); } while (0)

#endif // COMMON_HEADER
