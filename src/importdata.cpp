/******************************************************************************
 * src/importdata.cc
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

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>

#include "importdata.h"
#include "common.h"

//! check for RESULT line, returns offset of key=values
static inline size_t
is_result_line(const std::string& line)
{
    if (line.substr(0,6) == "RESULT" && isblank(line[6]))
        return 7;

    if (line.substr(0,9) == "// RESULT" && isblank(line[9]))
        return 10;

    if (line.substr(0,8) == "# RESULT" && isblank(line[8]))
        return 9;

    return 0;
}

//! split a string into "key=value" parts at TABs or spaces.
static inline std::vector<std::string>
split_result_line(const std::string& str)
{
    std::vector<std::string> out;

    // auto-select separation character
    char sep = (str.find('\t') != std::string::npos) ? '\t' : ' ';

    std::string::const_iterator it = str.begin();
    it += is_result_line(str);

    std::string::const_iterator last = it;

    for (; it != str.end(); ++it)
    {
        if (*it == sep)
        {
            if (last != it)
                out.push_back(std::string(last, it));
            last = it + 1;
        }
    }

    if (last != it)
        out.push_back(std::string(last, it));

    return out;
}

//! split a "key=value" string into key and value parts
static inline void
split_keyvalue(const std::string& field, size_t col,
               std::string& key, std::string& value,
               bool opt_colnums = false)
{
    std::string::size_type eqpos = field.find('=');
    if (eqpos == std::string::npos)
    {
        if (opt_colnums) {
            // add field as col#
            std::ostringstream os;
            os << "col" << col;
            key = os.str();
            value = field;
        }
        else {
            // else use field as boolean key
            key = field;
            value = "1";
        }
    }
    else {
        key = field.substr(0, eqpos);
        value = field.substr(eqpos+1);
    }
}

//! returns true if the give table exists.
bool ImportData::exist_table(const std::string& table)
{
    const char *paramValues[1];
    paramValues[0] = table.c_str();

    PGresult* r = PQexecParams(g_pg,
                               "SELECT COUNT(*) FROM pg_tables WHERE tablename = $1",
                               1, NULL, paramValues, NULL, NULL, 0);

    if (PQresultStatus(r) != PGRES_TUPLES_OK)
    {
        OUT("SELECT failed: " << PQerrorMessage(g_pg));
        PQclear(r);
        return false;
    }

    assert(PQntuples(r) == 1 && PQnfields(r) == 1);

    bool exist = ( strcmp(PQgetvalue(r,0,0), "1") == 0 );

    PQclear(r);
    return exist;
}

//! CREATE TABLE for the accumulated data set
bool ImportData::create_table() const
{
    if (exist_table(m_tablename))
    {
        OUT("Table \"" << m_tablename << "\" exists. Replacing data.");

        std::ostringstream cmd;
        cmd << "DROP TABLE \"" << m_tablename << "\"";

        PGresult* r = PQexec(g_pg, cmd.str().c_str());
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            OUT("DROP TABLE failed: " << PQerrorMessage(g_pg));
            PQclear(r);
            return false;
        }
    }

    if (mopt_verbose >= 1)
        OUT(m_fieldset.make_create_table(m_tablename));

    PGresult* r = PQexec(g_pg, m_fieldset.make_create_table(m_tablename).c_str());
    if (PQresultStatus(r) != PGRES_COMMAND_OK)
    {
        OUT("CREATE TABLE failed: " << PQerrorMessage(g_pg));
        PQclear(r);
        return false;
    }

    return true;
}

//! insert a line into the database table
bool ImportData::insert_line(const std::string& line)
{
    // check for duplicate lines
    if (mopt_noduplicates)
    {
        if (m_lineset.find(line) != m_lineset.end())
        {
            if (mopt_verbose >= 1)
                OUT("Dropping duplicate " << line);
            return true;
        }

        m_lineset.insert(line);
    }

    // split line and construct INSERT command
    slist_type slist = split_result_line(line);

    std::ostringstream cmd;
    cmd << "INSERT INTO \"" << m_tablename << "\" (";

    std::string paramValues[slist.size()];
    const char* paramValuesC[slist.size()];

    for (size_t i = 0; i < slist.size(); ++i)
    {
        if (slist[i].size() == 0) continue;
        std::string key;
        split_keyvalue(slist[i], i, key, paramValues[i]);

        if (i != 0) cmd << ",";
        cmd << "\"" << key << "\"";
        paramValuesC[i] = paramValues[i].c_str();
    }

    cmd << ") VALUES (";
    for (size_t i = 0; i < slist.size(); ++i)
    {
        if (i != 0) cmd << ",";
        cmd << "$" << (i+1);
    }
    cmd << ")";

    if (mopt_verbose >= 2) OUT(cmd.str());

    PGresult* r = PQexecParams(g_pg, cmd.str().c_str(),
                               slist.size(), NULL, paramValuesC, NULL, NULL, 1);

    if (PQresultStatus(r) != PGRES_COMMAND_OK)
    {
        OUT("INSERT failed: " << PQerrorMessage(g_pg));
        PQclear(r);
        return false;
    }
    else
    {
        PQclear(r);
        return true;
    }
}

//! process an input stream (file or stdin), cache lines or insert directly.
void ImportData::process_stream(std::istream& is)
{
    std::string line;

    while ( std::getline(is,line) )
    {
        if (!mopt_all_lines && is_result_line(line) == 0)
            continue;

        if (mopt_verbose >= 2)
            OUT("line: " << line);

        if (!mopt_firstline)
        {
            // split line and detect types of each field
            slist_type slist = split_result_line(line);

            size_t col = 0;
            for (slist_type::iterator si = slist.begin();
                 si != slist.end(); ++si, ++col)
            {
                if (si->size() == 0) continue;
                std::string key, value;
                split_keyvalue(*si, col, key, value);
                m_fieldset.add_field(key, value);
            }

            // cache line
            m_linedata.push_back(line);
            ++m_count, ++m_total_count;
        }
        else
        {
            if (m_total_count == 0)
            {
                // split line and detect types of each field
                slist_type slist = split_result_line(line);

                size_t col = 0;
                for (slist_type::iterator si = slist.begin();
                     si != slist.end(); ++si, ++col)
                {
                    if (si->size() == 0) continue;
                    std::string key, value;
                    split_keyvalue(*si, col, key, value);
                    m_fieldset.add_field(key, value);
                }

                // immediately create table from first row
                if (!create_table()) return;
            }

            if (insert_line(line)) {
                ++m_count, ++m_total_count;
            }
        }
    }
}

//! process cached data lines
void ImportData::process_linedata()
{
    if (!create_table()) return;

    for (slist_type::const_iterator line = m_linedata.begin();
         line != m_linedata.end(); ++line)
    {
        if (insert_line(*line)) {
            ++m_count, ++m_total_count;
        }
    }
}

//! initializing constructor
ImportData::ImportData()
    : mopt_verbose(gopt_verbose),
      mopt_firstline(false),
      mopt_all_lines(false),
      mopt_noduplicates(false),
      mopt_colnums(false),
      m_total_count(0)
{
}

//! print command line usage
void ImportData::print_usage(const std::string& progname)
{
    OUT("Usage: " << progname << " [-1] [-a] [-D] <table-name> [files...]" << std::endl <<
        std::endl <<
        "Options: " << std::endl <<
        "  -1       Take field types from first line and process stream." << std::endl <<
        "  -a       Process all line, regardless of RESULT marker." << std::endl <<
        "  -C       Enumerate unnamed fields with col# instead of using key names." << std::endl <<
        "  -D       Eliminate duplicate RESULT lines." << std::endl <<
        "  -v       Increase verbosity." << std::endl <<
        std::endl);

    exit(EXIT_FAILURE);
}

//! process command line arguments and data
int ImportData::main(int argc, char* argv[])
{
    FieldSet::check_detect();

    /* parse command line parameters */
    int opt;

    while ((opt = getopt(argc, argv, "h1avDC")) != -1) {
        switch (opt) {
        case '1':
            mopt_firstline = true;
            break;
        case 'a':
            mopt_all_lines = true;
            break;
        case 'v':
            mopt_verbose++;
            break;
        case 'D':
            mopt_noduplicates = true;
            break;
        case 'C':
            mopt_colnums = true;
            break;
        case 'h': default:
            print_usage(argv[0]);
        }
    }

    // no table name given
    if (optind == argc)
        print_usage(argv[0]);

    m_tablename = argv[optind++];

    // begin transaction
    {
        PGresult* r = PQexec(g_pg, "BEGIN TRANSACTION");
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            OUT("BEGIN TRANSACTION failed: " << PQerrorMessage(g_pg));
            PQclear(r);
            return -1;
        }
        PQclear(r);
    }

    // process file commandline arguments
    if (optind < argc)
    {
        while (optind < argc)
        {
            m_count = 0;
            std::ifstream in(argv[optind]);
            if (!in.good()) {
                OUT("Error reading " << argv[optind] << ": " << strerror(errno));
                return -1;
            }
            else {
                process_stream(in);

                if (mopt_firstline) {
                    OUT("Imported " << m_count << " rows of data from " << argv[optind]);
                }
                else {
                    OUT("Cached " << m_count << " rows of data from " << argv[optind]);
                }
            }
            ++optind;
        }
    }
    else // no file arguments -> process stdin
    {
        process_stream(std::cin);
    }

    // process cached data lines
    if (!mopt_firstline)
    {
        m_count = m_total_count = 0;
        process_linedata();
    }

    // begin transaction
    {
        PGresult* r = PQexec(g_pg, "COMMIT TRANSACTION");
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            OUT("COMMIT TRANSACTION failed: " << PQerrorMessage(g_pg));
            PQclear(r);
            return -1;
        }
        PQclear(r);
    }

    OUT("Imported in total " << m_total_count << " rows of data containing " << m_fieldset.count() << " fields each.");

    return 0;
}
