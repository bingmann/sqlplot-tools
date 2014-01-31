/******************************************************************************
 * src/common.cpp
 *
 * Common global variables across all programs.
 *
 ******************************************************************************
 * Copyright (C) 2013-2014 Timo Bingmann <tb@panthema.net>
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

#include "common.h"

//! verbosity, common global option.
int gopt_verbose = 0;

//! check processed output matches the output file
bool gopt_check_output = false;

//! global SQL datbase connection handle
SqlDatabase* g_db = NULL;

#include "pgsql.h"
#include "mysql.h"
#include "sqlite.h"

//! initialize global SQL database connection
bool g_db_initialize()
{
    g_db_free();

    //! first try to connect to a PostgreSQL database

    g_db = new PgSqlDatabase;
    if (g_db->initialize())
        return true;
    delete g_db;

    g_db = new MySqlDatabase;
    if (g_db->initialize())
        return true;
    delete g_db;

    g_db = new SQLiteDatabase;
    if (g_db->initialize())
        return true;
    delete g_db;

    return false;
}

//! free global SQL database connection
void g_db_free()
{
    if (g_db) {
        delete g_db;
        g_db = NULL;
    }
}
