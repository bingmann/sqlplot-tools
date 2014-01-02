/******************************************************************************
 * src/importdata.h
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

#ifndef IMPORTDATA_HEADER
#define IMPORTDATA_HEADER

#include "fieldset.h"

#include <set>

//! Encapsules one sp-importdata processes, which can also be run from other
//! sp-processors.
class ImportData
{
protected:

    //! verbosity
    bool mopt_verbose;
    
    //! take field types from first line and process stream
    bool mopt_firstline;

    //! parse all lines as key=value lines, ignoring RESULT flags
    bool mopt_all_lines;

    //! skip duplicate RESULT lines
    bool mopt_noduplicates;

    //! add numeric column numbers for key-less values (without equal sign)
    bool mopt_colnums;

    //! use TEMPORARY in CREATE TABLE for transient data
    bool mopt_temporary_table;

    //! table imported
    std::string m_tablename;

    //! field set of all imported data
    FieldSet m_fieldset;

    //! type of array of all data key=value lines
    typedef std::vector<std::string> slist_type;

    //! array of all data key=value lines
    slist_type m_linedata;

    //! sorted set of all data lines, for mopt_noduplicates
    std::set<std::string> m_lineset;

    //! number of RESULT lines counted in current file
    size_t m_count;

    //! number of RESULT lines counted over all files
    size_t m_total_count;

public:
    //! initializing constructor
    ImportData(bool temporary_table = false);

    //! returns true if the give table exists.
    static bool exist_table(const std::string& table);

    //! CREATE TABLE for the accumulated data set
    bool create_table() const;

    //! insert a line into the database table
    bool insert_line(const std::string& line);

    //! process an input stream (file or stdin), cache lines or insert directly.
    void process_stream(std::istream& is);

    //! process cached data lines
    void process_linedata();

    //! print command line usage
    int print_usage(const std::string& progname);

    //! process command line arguments and data
    int main(int argc, char* const argv[]);
};

#endif // IMPORTDATA_HEADER
