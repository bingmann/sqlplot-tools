/******************************************************************************
 * src/sp-importdata.cc
 *
 * Import RESULT files into the local PostgreSQL database for further
 * processing. Automatically detects the SQL column types.
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

#include "importdata.h"
#include "common.h"
#include <stdlib.h>

int main(int argc, char* argv[])
{
    // make connection to the database
    g_pg = PQconnectdb("");

    // check to see that the backend connection was successfully made
    if (PQstatus(g_pg) != CONNECTION_OK)
    {
        OUT("Connection to database failed: " << PQerrorMessage(g_pg));
        return -1;
    }

    try {
        ImportData().main(argc, argv);
    }
    catch (std::runtime_error& e)
    {
        OUT(e.what());
        return EXIT_FAILURE;
    }


    PQfinish(g_pg);
    return EXIT_SUCCESS;
}
