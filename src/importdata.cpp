/******************************************************************************
 * src/importdata.cpp
 *
 * Import RESULT files into the local PostgreSQL database for further
 * processing. Automatically detects the SQL column types.
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

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <glob.h>

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

//! deduplicate key names by appending numbers
static inline std::string
dedup_key(const std::string& key, std::set<std::string>& keyset)
{
    // unique key
    if (keyset.find(key) == keyset.end())
    {
        keyset.insert(key);
        return key;
    }

    // append numbers to make unique
    for (size_t num = 1; ; ++num)
    {
        std::ostringstream nkey;
        nkey << key << num;

        if (keyset.find(nkey.str()) == keyset.end())
        {
            keyset.insert(nkey.str());
            return nkey.str();
        }
    }

    abort();
}

//! CREATE TABLE for the accumulated data set
bool ImportData::create_table() const
{
    if (g_db->exist_table(m_tablename))
    {
        OUT("Table \"" << m_tablename << "\" exists. Replacing data.");

        std::ostringstream cmd;
        cmd << "DROP TABLE " << g_db->quote_field(m_tablename);

        g_db->execute(cmd.str());
    }

    std::string createtable = m_fieldset.make_create_table(m_tablename, mopt_temporary_table);

    if (mopt_verbose >= 1)
        OUT(createtable);

    try
    {
        g_db->execute(createtable);
    }
    catch (std::runtime_error &e)
    {
        if (g_db->type() == SqlDatabase::DB_MYSQL)
        {
            // in MySQL there is no way to check for existing TEMPORARY TABLES,
            // so we just DROP TABLE and retry CREATE TABLE if it fails onces.

            OUT("Table \"" << m_tablename << "\" maybe exists. Replacing data.");

            std::ostringstream cmd;
            cmd << "DROP TABLE " << g_db->quote_field(m_tablename);

            g_db->execute(cmd.str());

            g_db->execute(createtable);
        }
        else {
            throw; // other databases have real errors.
        }
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

    std::set<std::string> keyset;

    std::ostringstream cmd;
    cmd << "INSERT INTO " << g_db->quote_field(m_tablename) << " (";

    std::vector<std::string> paramValues(slist.size());

    for (size_t i = 0; i < slist.size(); ++i)
    {
        if (slist[i].size() == 0) continue;

        std::string key;
        split_keyvalue(slist[i], i, key, paramValues[i]);

        key = dedup_key(key, keyset);

        if (i != 0) cmd << ',';
        cmd << g_db->quote_field(key);
    }

    cmd << ") VALUES (";
    for (size_t i = 0; i < slist.size(); ++i)
    {
        if (i != 0) cmd << ',';
        cmd << g_db->placeholder(i);
    }
    cmd << ')';

    if (mopt_verbose >= 2) OUT(cmd.str());

    g_db->query(cmd.str(), paramValues);

    return true;
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

        std::set<std::string> keyset;

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
                key = dedup_key(key, keyset);
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
                    key = dedup_key(key, keyset);
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
ImportData::ImportData(bool temporary_table)
    : mopt_verbose(gopt_verbose),
      mopt_firstline(false),
      mopt_all_lines(false),
      mopt_noduplicates(false),
      mopt_colnums(false),
      mopt_temporary_table(temporary_table),
      mopt_empty_okay(false),
      m_total_count(0)
{
}

//! print command line usage
int ImportData::print_usage(const std::string& progname)
{
    OUT("Usage: " << progname << " [options] <table-name> [files...]" << std::endl <<
        std::endl <<
        "Options: " << std::endl <<
        "  -1       Take field types from first line and process stream." << std::endl <<
        "  -a       Process all line, regardless of RESULT marker." << std::endl <<
        "  -C       Enumerate unnamed fields with col# instead of using key names." << std::endl <<
        "  -E       Allow empty tables or globs without matching files." << std::endl <<
        "  -d       Eliminate duplicate RESULT lines." << std::endl <<
        "  -T       Import into TEMPORARY table (for in-file processing)." << std::endl <<
        "  -P       Import into non-TEMPORARY table (reverts the default -T)." << std::endl <<
        "  -v       Increase verbosity." << std::endl);

    return EXIT_FAILURE;
}

//! process command line arguments and data
int ImportData::main(int argc, char* const argv[])
{
    FieldSet::check_detect();

    int save_optind = 1;
    std::swap(optind, save_optind);

    /* parse command line parameters */
    int opt;

    while ((opt = getopt(argc, argv, "h1avdCETPD:")) != -1) {
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
        case 'd':
            mopt_noduplicates = true;
            break;
        case 'C':
            mopt_colnums = true;
            break;
        case 'E':
            mopt_empty_okay = true;
            break;
        case 'T':
            mopt_temporary_table = true;
            break;
        case 'P':
            mopt_temporary_table = false;
            break;
        case 'D':
            gopt_db_connection = optarg;
            break;
        case 'h': default:
            return print_usage(argv[0]);
        }
    }

    // no table name given
    if (optind == argc)
        print_usage(argv[0]);

    m_tablename = argv[optind++];

    // maybe connect to database
    bool opt_dbconnect = false;
    if (!g_db)
    {
        if (!g_db_initialize())
            OUT_THROW("Fatal: could not connect to a SQL database");
        opt_dbconnect = true;
    }

    // begin transaction
    g_db->execute("BEGIN");

    // process file commandline arguments
    if (optind < argc)
    {
        while (optind < argc)
        {
            // glob() for matching file names
            glob_t globbuf;

            int gflags = GLOB_TILDE | GLOB_BRACE;
            if (mopt_empty_okay) gflags |= GLOB_NOCHECK;

            int gr = glob(argv[optind], gflags, NULL, &globbuf);
            if (gr != 0) {
                OUT_THROW("Error globing " << argv[optind] << ": " << strerror(errno));
            }

            for (size_t gi = 0; gi < globbuf.gl_pathc; ++gi)
            {
                const char* fname = globbuf.gl_pathv[gi];

                m_count = 0;
                std::ifstream in(fname);
                if (!in.good()) {
                    if (mopt_empty_okay)
                        OUT("Error reading " << fname << ": " << strerror(errno));
                    else
                        OUT_THROW("Error reading " << fname << ": " << strerror(errno));
                }
                else {
                    process_stream(in);

                    if (mopt_firstline) {
                        OUT("Imported " << m_count << " rows of data from " << fname);
                    }
                    else {
                        OUT("Cached " << m_count << " rows of data from " << fname);
                    }
                }
            }

            globfree(&globbuf);
            ++optind;
        }
    }
    else // no file arguments -> process stdin
    {
        OUT("Reading data from stdin ...");

        process_stream(std::cin);
    }

    std::swap(optind, save_optind);

    // process cached data lines
    if (!mopt_firstline)
    {
        m_count = m_total_count = 0;
        process_linedata();
    }

    // finish transaction
    g_db->execute("COMMIT");

    OUT("Imported in total " << m_total_count << " rows of data containing " << m_fieldset.count() << " fields each.");

    if (opt_dbconnect)
        g_db_free();

    return EXIT_SUCCESS;
}
