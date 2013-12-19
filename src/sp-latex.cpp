/******************************************************************************
 * src/sp-latex.cc
 *
 * Process embedded SQL plot instructions in LaTeX files.
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

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <getopt.h>
#include <boost/regex.hpp>

#include "common.h"
#include "strtools.h"
#include "textlines.h"
#include "importdata.h"

TextLines g_lines;

static inline int
is_comment_line(const std::string& line)
{
    int i = 0;
    while (isblank(line[i])) ++i;
    return (line[i] == '%') ? i : -1;
}

std::string format_sqloutput(PGresult* r)
{
    int colnum = PQnfields(r);
    int rownum = PQntuples(r);

    // format SQL table: determine sizes

    std::vector<size_t> width(colnum, 0);

    for (int col = 0; col < colnum; ++col)
    {
        width[col] = std::max(width[col], strlen( PQfname(r,col) ));

        assert(PQfformat(r,col) == 0);
    }

    for (int row = 0; row < rownum; ++row)
    {
        for (int col = 0; col < colnum; ++col)
        {
            width[col] = std::max(width[col], strlen( PQgetvalue(r,row,col) ));
        }
    }

    // construct header/middle/footer breaks
    std::ostringstream obreak;
    obreak << "+-";
    for (int col = 0; col < colnum; ++col)
    {
        if (col != 0) obreak << "+-";
        obreak << std::string(width[col]+1, '-');
    }
    obreak << "+" << std::endl;

    // format output
    std::ostringstream os;

    os << obreak.str();

    os << "| ";
    for (int col = 0; col < colnum; ++col)
    {
        if (col != 0) os << "| ";
        os << std::setw(width[col]) << std::right
           << PQfname(r, col) << ' ';
    }
    os << "|" << std::endl;
    os << obreak.str();

    for (int row = 0; row < rownum; ++row)
    {
         os << "| ";
        for (int col = 0; col < colnum; ++col)
        {
            if (col != 0) os << "| ";
            os << std::setw(width[col]);

            //os << '-' << PQftype(r,col) << '-';
            if (PQftype(r,col) == 23 || PQftype(r,col) == 20)
                os << std::right;
            else
                os << std::left;

            os << PQgetvalue(r, row, col) << ' ';
        }
        os << "|" << std::endl;
    }
    os << obreak.str();

    return os.str();
}

ssize_t scan_lines_for_comment(size_t ln, const std::string& cprefix)
{
    // scan lines forward till next comment line
    size_t cln = ln;
    while ( cln < g_lines.size() && is_comment_line(g_lines[cln]) < 0 )
    {
        ++cln;
    }

    // EOF reached -> no matching comment lien
    if ( cln >= g_lines.size() )
        return -1;

    std::string comment = g_lines[cln].substr( is_comment_line(g_lines[cln])+1 );
    comment = trim(comment);

    return is_prefix(comment, cprefix) ? cln : -1;
}

//! Process % SQL commands
void process_sql(size_t /* ln */, const std::string& cmdline)
{
    PGresult* r = PQexec(g_pg, cmdline.c_str());
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
    {
        OUT("SQL command successful.");
    }
    else if (PQresultStatus(r) == PGRES_TUPLES_OK)
    {
        OUT("SQL returned tuples.");
    }
    else
    {
        OUT("SQL failed: " << PQerrorMessage(g_pg));
        exit(1);
    }

    PQclear(r);
}

//! Process % IMPORTDATA commands
void process_importdata(size_t /* ln */, const std::string& cmdline)
{
    // split argument at whitespaces
    std::vector<std::string> args = split_ws(cmdline);

    // convert std::strings to char*
    char* argv[args.size() + 1];

    for (size_t i = 0; i < args.size(); ++i)
        argv[i] = (char*)args[i].c_str();

    argv[args.size()] = NULL;

    ImportData().main(args.size(), argv);
}

//! Process % TEXTTABLE commands
void process_texttable(size_t ln, const std::string& cmdline)
{
    PGresult* r = PQexec(g_pg, cmdline.c_str());
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
    {
        OUT("SQL failed: " << PQerrorMessage(g_pg));
        exit(1);
        return;
    }

    // format result as a text table
    std::string output = format_sqloutput(r);
    PQclear(r);

    output += "% END TEXTTABLE " + shorten(cmdline) + "\n";

    // find following "% END TEXTTABLE" and replace enclosing lines
    ssize_t eln = scan_lines_for_comment(ln, "END TEXTTABLE");

    if (eln < 0) {
        g_lines.replace(ln, ln, output, "TEXTTABLE");
    }
    else {
        g_lines.replace(ln, eln+1, output, "TEXTTABLE");
    }
}

//! Process % PLOT commands
void process_plot(size_t ln, const std::string& cmdline)
{
    PGresult* r = PQexec(g_pg, cmdline.c_str());
    if (PQresultStatus(r) != PGRES_TUPLES_OK)
    {
        OUT("SQL failed: " << PQerrorMessage(g_pg));
        exit(1);
        return;
    }

    int colnum = PQnfields(r);
    int rownum = PQntuples(r);

#if 0
    // read column names
    std::map<std::string, unsigned int> colmap;

    for (size_t i = 0; i < colnum; ++i)
        colmap[ PQfname(r,i) ] = i;

    // check for existing x and y columns.
    if (colmap.find("x") == colmap.end()) {
        OUT("PLOT failed: result contains no 'x' column.");
        exit(1);
        return;
    }

    if (colmap.find("y") == colmap.end()) {
        OUT("PLOT failed: result contains no 'y' column.");
        exit(1);
        return;
    }

    // construct coordinates {...} clause
    int colx = colmap["x"], coly = colmap["y"];
#endif

    std::ostringstream oss;
    oss << "coordinates {";
    for (int row = 0; row < rownum; ++row)
    {
        oss << " (";
        for (int col = 0; col < colnum; ++col)
        {
            if (col != 0) oss << ',';
            oss << PQgetvalue(r, row, col);
        }
        oss << ')';
    }
    oss << " };";

    // check whether line contains an \addplot command
    static const boost::regex
        re_addplot("([:blank:]*\\\\addplot.* )coordinates \\{[0-9.,() +-e]+\\};(.*)");
    boost::smatch rm_addplot;

    if (ln < g_lines.size() &&
        boost::regex_match(g_lines[ln], rm_addplot, re_addplot))
    {
        assert(rm_addplot.size() == 3);
        std::string output = rm_addplot[1].str() + oss.str() + rm_addplot[2].str();
        g_lines.replace(ln, ln+1, output, "PLOT");
    }
    else
    {
        std::string output = "\\addplot " + oss.str();
        g_lines.replace(ln, ln, output, "PLOT");
    }
}

//! process line-based file in place
void process()
{
    // iterate over all lines
    for (size_t ln = 0; ln < g_lines.size();)
    {
        // try to collect an aligned comment block
        int cin_first = is_comment_line(g_lines[ln]);
        if (cin_first < 0) {
            ++ln;
            continue;
        }

        std::string cmdline = g_lines[ln++].substr(cin_first+1);

        // collect lines while they are at the same indentation level
        while ( ln < g_lines.size() &&
                is_comment_line(g_lines[ln]) == cin_first )
        {
            cmdline += g_lines[ln++].substr(cin_first+1);
        }

        cmdline = trim(cmdline);

        // extract first word
        std::string::size_type space_pos = cmdline.find(' ');
        std::string first_word = cmdline.substr(0, space_pos);

        if (first_word == "SQL")
        {
            OUT("==> " << cmdline);
            process_sql(ln, cmdline.substr(space_pos+1));
        }
        else if (first_word == "IMPORTDATA")
        {
            OUT("==> " << cmdline);
            process_importdata(ln, cmdline);
        }
        else if (first_word == "TEXTTABLE")
        {
            OUT("--> " << cmdline);
            process_texttable(ln, cmdline.substr(space_pos+1));
        }
        else if (first_word == "PLOT")
        {
            OUT("--> " << cmdline);
            process_plot(ln, cmdline.substr(space_pos+1));
        }
    }
}

//! process a stream
void process_stream(const std::string& filename, std::istream& is)
{
    // read complete LaTeX file line-wise
    g_lines.read_stream(is);

    // process lines in place
    process();

    OUT("--- Finished processing " << filename << " successfully.");
}

//! print command line usage
void print_usage(const std::string& progname)
{
    OUT("Usage: " << progname << " [options] [files...]" << std::endl <<
        std::endl <<
        "Options: " << std::endl <<
        "  -v         Increase verbosity." << std::endl <<
        "  -o <file>  Output all processed files to this stream." << std::endl <<
        "  -C         Verify that -o output file matches processed data (for tests)." << std::endl <<
        std::endl);

    exit(EXIT_FAILURE);
}

//! main(), yay.
int main(int argc, char* argv[])
{
    // parse command line parameters
    int opt;

    //! output file name
    std::string opt_outputfile;

    //! check processed output matches the output file
    bool opt_check_outputfile = false;

    while ((opt = getopt(argc, argv, "vo:C")) != -1) {
        switch (opt) {
        case 'v':
            gopt_verbose++;
            break;
        case 'o':
            opt_outputfile = optarg;
            break;
        case 'C':
            opt_check_outputfile = true;
            break;
        case 'h': default:
            print_usage(argv[0]);
        }
    }

    // make connection to the database
    g_pg = PQconnectdb("");

    // check to see that the backend connection was successfully made
    if (PQstatus(g_pg) != CONNECTION_OK)
    {
        OUT("Connection to database failed: " << PQerrorMessage(g_pg));
        return -1;
    }

    // open output file or string stream
    std::ostream* output = NULL;
    if (opt_check_outputfile)
    {
        if (!opt_outputfile.size()) {
            OUT("Error: checking output requires and output filename.");
            return -1;
        }

        output = new std::ostringstream;
    }
    else if (opt_outputfile.size())
    {
        output = new std::ofstream(opt_outputfile.c_str());

        if (!output->good()) {
            OUT("Error opening output stream: " << strerror(errno));
            return -1;
        }
    }

    // process file commandline arguments
    if (optind < argc)
    {
        while (optind < argc)
        {
            const char* filename = argv[optind];

            std::ifstream in(filename);
            if (!in.good()) {
                OUT("Error reading " << filename << ": " << strerror(errno));
                return -1;
            }
            else {
                process_stream(filename, in);

                if (output)  {
                    // write to common output
                    g_lines.write_stream(*output);
                }
                else {
                    // overwrite input file
                    in.close();
                    std::ofstream out(filename);
                    if (!out.good()) {
                        OUT("Error writing " << filename << ": " << strerror(errno));
                        return -1;
                    }

                    g_lines.write_stream(out);
                    if (!out.good()) {
                        OUT("Error writing " << filename << ": " << strerror(errno));
                        return -1;
                    }
                }
            }
            ++optind;
        }
    }
    else // no file arguments -> process stdin
    {
        OUT("Reading stdin ...");
        process_stream("stdin", std::cin);

        if (output)  {
            // write to common output
            g_lines.write_stream(*output);
        }
        else {
            // write to stdout
            g_lines.write_stream(std::cout);
        }
    }

    // verify processed output against file
    if (opt_check_outputfile)
    {
        std::ifstream in(opt_outputfile.c_str());
        if (!in.good()) {
            OUT("Error reading " << opt_outputfile << ": " << strerror(errno));
            return -1;
        }
        std::string checkdata = read_stream(in);

        assert(output);
        std::ostringstream* oss = (std::ostringstream*)output;

        if (checkdata != oss->str())
        {
            OUT("Mismatch to expected output file " << opt_outputfile);
            return -1;
        }
    }

    if (output) delete output;

    PQfinish(g_pg);
    return 0;
}
