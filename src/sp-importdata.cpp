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

#include <libpq-fe.h>

// global options and parameters

const char* g_tablename = NULL;

int gopt_verbose = 0;
bool gopt_oneshot = false;
bool gopt_all_lines = false;
bool gopt_noduplicates = false;
bool gopt_colnums = false;
size_t g_count = 0;
size_t g_total_count = 0;

PGconn* g_pg;

typedef std::vector<std::string> slist_type;

// check for result line
int is_result_line(const std::string& line)
{
    if (line.substr(0,6) == "RESULT" && isblank(line[6]))
        return 7;

    if (line.substr(0,9) == "// RESULT" && isblank(line[9]))
        return 10;

    if (line.substr(0,8) == "# RESULT" && isblank(line[8]))
        return 9;

    return false;
}

static inline slist_type split_line(const std::string& str)
{
    slist_type out;

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

static inline void split_keyvalue(const std::string& field, size_t col, std::string& key, std::string& value)
{
    std::string::size_type eqpos = field.find('=');
    if (eqpos == std::string::npos)
    {
        if (gopt_colnums) {
            // add field as col#
            std::ostringstream os; os << "col" << col;
            key = os.str();
            value = field;
        }
        else { // else use field as boolean key
            key = field;
            value = "1";
        }
    }
    else {
        key = field.substr(0, eqpos);
        value = field.substr(eqpos+1);
    }
}

struct FieldSet
{
    enum fieldtype { T_NONE, T_VARCHAR, T_DOUBLE, T_INTEGER };

    typedef std::pair<std::string, fieldtype> pair_type;

    typedef std::vector<pair_type> fieldset_type;

    fieldset_type m_fieldset;

    static inline const char* fieldname(fieldtype t)
    {
        switch (t) {
        default:
        case T_NONE: return "NONE";
        case T_VARCHAR: return "VARCHAR";
        case T_DOUBLE: return "DOUBLE PRECISION";
        case T_INTEGER: return "BIGINT";
        }
    }

    inline size_t count() const
    {
        return m_fieldset.size();
    }

    static inline fieldtype detect(const std::string& str)
    {
        std::string::const_iterator it = str.begin();

        // skip sign
        if (it != str.end() && (*it == '+' || *it == '-')) ++it;

        // iterate over digits
        while (it != str.end() && isdigit(*it)) ++it;

        if (it == str.end() && it != str.begin()) {
            return T_INTEGER;
        }
        
        // skip decimal and iterate over digits
        if (it != str.end() && *it == '.') {
            ++it;

            // iterate over digits
            while (it != str.end() && isdigit(*it)) ++it;

            if (it == str.end() && it != str.begin()) {
                return T_DOUBLE;
            }

            // check double exponent
            if (it != str.end() && (*it == 'e' || *it == 'E')) {
                ++it;

                // skip sign
                if (it != str.end() && (*it == '+' || *it == '-')) ++it;

                // iterate over digits
                while (it != str.end() && isdigit(*it)) ++it;

                if (it == str.end() && it != str.begin()) {
                    return T_DOUBLE;
                }
            }
        }

        return T_VARCHAR;
    }

    static inline void check_detect()
    {
        assert( detect("1234") == T_INTEGER );
        assert( detect("1234.3") == T_DOUBLE );
        assert( detect(".3e-3") == T_DOUBLE );
        assert( detect("1234,3") == T_VARCHAR );
        assert( detect("sdfdf") == T_VARCHAR );
    }

    void add_field(const std::string& key, const std::string& value)
    {
        fieldtype t = detect(value);

        for (fieldset_type::iterator fi = m_fieldset.begin();
             fi != m_fieldset.end(); ++fi)
        {
            if (fi->first == key) // found matching entry
            {
                if (fi->second > t) {
                    fi->second = t;
                }
                return;
            }
        }

        // add new entry
        m_fieldset.push_back( pair_type(key,t) );
    }

    std::string make_create_table()
    {
        std::ostringstream os;
        os << "CREATE TABLE \"" << g_tablename << "\" (";

        for (fieldset_type::const_iterator fi = m_fieldset.begin();
             fi != m_fieldset.end(); ++fi)
        {
            if (fi != m_fieldset.begin())
                os << ", ";
            os << "\"" << fi->first << "\" " << fieldname(fi->second);
        }

        os << ")";
        return os.str();
    }

    static bool exist_table(const char* table)
    {
        const char *paramValues[1];
        paramValues[0] = table;

        PGresult* r
            = PQexecParams(g_pg,
                           "SELECT COUNT(*) FROM pg_tables WHERE tablename = $1",
                           1, NULL, paramValues, NULL, NULL, 0);

        if (PQresultStatus(r) != PGRES_TUPLES_OK)
        {
            std::cerr << "SELECT failed: " << PQerrorMessage(g_pg);
            PQclear(r);
            return false;
        }

        assert(PQntuples(r) == 1 && PQnfields(r) == 1);

        bool exist = ( strcmp(PQgetvalue(r,0,0), "1") == 0 );
        
        PQclear(r);
        return exist;
    }

    bool create_table()
    {
        if (exist_table(g_tablename))
        {
            std::cerr << "Table \"" << g_tablename << "\" exists. Replacing data.\n";

            std::ostringstream cmd;
            cmd << "DROP TABLE \"" << g_tablename << "\"";

            PGresult* r = PQexec(g_pg, cmd.str().c_str());
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::cerr << "DROP TABLE failed: " << PQerrorMessage(g_pg);
                PQclear(r);
                return false;
            }
        }

        if (gopt_verbose >= 1)
            std::cerr << make_create_table() << "\n";

        PGresult* r = PQexec(g_pg, make_create_table().c_str());
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            std::cerr << "CREATE TABLE failed: " << PQerrorMessage(g_pg);
            PQclear(r);
            return false;
        }

        return true;
    }
};

FieldSet g_fieldset;

slist_type g_linedata;

bool insert_line(const std::string& line)
{
    // check for duplicate lines
    if (gopt_noduplicates)
    {
        static std::set<std::string> lineset;
        if (lineset.find(line) != lineset.end())
        {
            if (gopt_verbose >= 1)
                std::cout << "Dropping duplicate " << line << std::endl;
            return true;
        }

        lineset.insert(line);
    }

    // split line and construct INSERT command
    slist_type slist = split_line(line);

    std::ostringstream cmd;
    cmd << "INSERT INTO \"" << g_tablename << "\" (";

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

    if (gopt_verbose >= 2)
        std::cout << cmd.str() << "\n";

    PGresult* r = PQexecParams(g_pg, cmd.str().c_str(),
                               slist.size(), NULL, paramValuesC, NULL, NULL, 1);

    if (PQresultStatus(r) != PGRES_COMMAND_OK)
    {
        std::cerr << "INSERT failed: " << PQerrorMessage(g_pg);
        PQclear(r);
        return false;
    }
    else
    {
        PQclear(r);
        return true;
    }
}

void process_stream(std::istream& is)
{
    std::string line;

    while ( std::getline(is,line) )
    {
        if (!gopt_all_lines && is_result_line(line) == 0)
            continue;

        if (gopt_verbose >= 2)
            std::cout << "line: " << line << "\n";

        if (!gopt_oneshot)
        {
            // split line and detect types of each field
            slist_type slist = split_line(line);
            
            size_t col = 0;
            for (slist_type::iterator si = slist.begin();
                 si != slist.end(); ++si, ++col)
            {
                if (si->size() == 0) continue;
                std::string key, value;
                split_keyvalue(*si, col, key, value);
                g_fieldset.add_field(key, value);
            }

            // cache line
            g_linedata.push_back(line);
            ++g_count, ++g_total_count;
        }
        else
        {
            if (g_total_count == 0)
            {
                // split line and detect types of each field
                slist_type slist = split_line(line);

                size_t col = 0;
                for (slist_type::iterator si = slist.begin();
                     si != slist.end(); ++si, ++col)
                {
                    if (si->size() == 0) continue;
                    std::string key, value;
                    split_keyvalue(*si, col, key, value);
                    g_fieldset.add_field(key, value);
                }
                
                // immediately create table from first row
                if (!g_fieldset.create_table()) return;
            }

            if (insert_line(line)) {
                ++g_count, ++g_total_count;
            }
        }
    }
}

void process_linedata()
{
    if (!g_fieldset.create_table()) return;

    for (slist_type::const_iterator line = g_linedata.begin();
         line != g_linedata.end(); ++line)
    {
        if (insert_line(*line)) {
            ++g_count, ++g_total_count;
        }
    }
}

// print command line usage
void print_usage(char* argv[])
{
    fprintf(stderr,
            "Usage: %s [-1] [-a] [-D] <table-name> [files...]\n"
            "\n"
            "Options: \n"
            "  -1       Take field types from first line and process stream.\n"
            "  -a       Process all line, regardless of RESULT marker.\n"
            "  -C       Enumerate unnamed fields with col# instead of using key names.\n"
            "  -D       Eliminate duplicate RESULT lines.\n"
            "  -v       Increase verbosity.\n"
            "\n",
            argv[0]);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    FieldSet::check_detect();

    /* parse command line parameters */
    int opt;

    while ((opt = getopt(argc, argv, "h1avDC")) != -1) {
        switch (opt) {
        case '1':
            gopt_oneshot = true;
            break;
        case 'a':
            gopt_all_lines = true;
            break;
        case 'v':
            gopt_verbose++;
            break;
        case 'D':
            gopt_noduplicates = true;
            break;
        case 'C':
            gopt_colnums = true;
            break;
        case 'h': default:
            print_usage(argv);
        }
    }

    // no table name given
    if (optind == argc)
        print_usage(argv);

    g_tablename = argv[optind++];

    // make connection to the database
    g_pg = PQconnectdb("");

    // check to see that the backend connection was successfully made
    if (PQstatus(g_pg) != CONNECTION_OK)
    {
        std::cerr << "Connection to database failed: " << PQerrorMessage(g_pg) << "\n";
        return -1;
    }

    // begin transaction
    {
        PGresult* r = PQexec(g_pg, "BEGIN TRANSACTION");
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            std::cerr << "BEGIN TRANSACTION failed: " << PQerrorMessage(g_pg);
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
            g_count = 0;
            std::ifstream in(argv[optind]);
            if (!in.good()) {
                std::cerr << "Error reading " << argv[optind] << ": " << strerror(errno) << "\n";
                return -1;
            }
            else {
                process_stream(in);

                if (gopt_oneshot) {
                    std::cerr << "Imported " << g_count << " rows of data from " << argv[optind] << "\n";
                }
                else {
                    std::cerr << "Cached " << g_count << " rows of data from " << argv[optind] << "\n";
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
    if (!gopt_oneshot)
    {
        g_count = g_total_count = 0;
        process_linedata();
    }

    // begin transaction
    {
        PGresult* r = PQexec(g_pg, "COMMIT TRANSACTION");
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            std::cerr << "COMMIT TRANSACTION failed: " << PQerrorMessage(g_pg);
            PQclear(r);
            return -1;
        }
        PQclear(r);
    }

    std::cerr << "Imported in total " << g_total_count << " rows of data containing " << g_fieldset.count() << " fields each.\n";
    
    PQfinish(g_pg);
    return 0;
}
