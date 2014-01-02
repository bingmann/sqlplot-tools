/******************************************************************************
 * src/sp-gnuplot.cc
 *
 * Process embedded SQL plot instructions in Gnuplot files.
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
#include "sql.h"
#include "textlines.h"
#include "importdata.h"

TextLines g_lines;

// *** current gnuplot datafile ***

static std::ostream* g_datafile;
static std::string g_datafilename;
static unsigned int g_dataindex;

// scan line for Gnuplot comment, returns index of # or -1 if the line is not a
// plain comment
inline int
is_comment_line(const std::string& line)
{
    int i = 0;
    while (isblank(line[i])) ++i;
    return (line[i] == '#') ? i : -1;
}

//! scan for next comment line with given prefix
static inline ssize_t
scan_lines_for_comment(size_t ln, const std::string& cprefix)
{
    return g_lines.scan_for_comment<is_comment_line>(ln, cprefix);
}

//! Process # SQL commands
static inline void
process_sql(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("SQL command successful.");
}

//! Process # IMPORT-DATA commands
static inline void
process_importdata(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    // split argument at whitespaces
    std::vector<std::string> args = split_ws(cmdline);

    // convert std::strings to char*
    char* argv[args.size() + 1];

    for (size_t i = 0; i < args.size(); ++i)
        argv[i] = (char*)args[i].c_str();

    argv[args.size()] = NULL;

    ImportData(true).main(args.size(), argv);
}

//! Helper to rewrite Gnuplot "plot" directives with new datafile/index pairs
struct GnuplotDataset
{
    unsigned int index;
    std::string title;
};

static inline void
gnuplot_plot_rewrite(size_t ln, size_t indent,
                     const std::vector<GnuplotDataset>& datasets,
                     const char* plot_type)
{
    std::ostringstream oss;

    // check whether line contains an "plot" command
    static const boost::regex re_plot("[[:blank:]]*plot.*\\\\[[:blank:]]*");

    if (ln >= g_lines.size() ||
        !boost::regex_match(g_lines[ln], re_plot))
    {
        // no "plot" command: construct default version from scratch

        oss << "plot";
        for (size_t i = 0; i < datasets.size(); ++i)
        {
            if (i != 0) oss << ',';
            oss << " \\" << std::endl
                << "    '" << g_datafilename << "' index " << datasets[i].index
                << " with linespoints";
        }
        oss << std::endl;

        g_lines.replace(ln, ln, indent, oss.str(), plot_type);
        return;
    }

    // scan following lines for plot descriptions
    static const boost::regex re_line("[[:blank:]]*'[^']+' index [0-9]+( .*?)(, \\\\)?[[:blank:]]*");
    boost::smatch rm;

    oss << "plot";

    size_t eln = ln+1;
    size_t entry = 0; // dataset entry

    while (eln < g_lines.size() &&
           boost::regex_match(g_lines[eln], rm, re_line))
    {
        // copy properties to new plot line
        if (entry < datasets.size())
        {
            if (entry != 0) oss << ',';
            oss << " \\" << std::endl
                << "    '" << g_datafilename << "' index " << datasets[entry].index;

            // if properties do not contain a title (or notitle), add it
            if (rm[1].str().find("title ") == std::string::npos &&
                datasets[entry].title.size())
            {
                oss << " title \"" << datasets[entry].title << '"';
            }

            // output extended properties
            oss << rm[1];

            // break if no \ was found at the end
            if (rm[2].length() == 0) {
                ++eln;
                break;
            }

            ++entry;
        }
        else
        {
            // gobble additional plot line
        }

        ++eln;
    }

    // append missing plot descriptions
    while (entry < datasets.size())
    {
        if (entry != 0) oss << ',';
        oss << " \\" << std::endl
            << "    '" << g_datafilename << "' index " << datasets[entry].index;

        if (datasets[entry].title.size())
            oss << " title \"" << datasets[entry].title << '"';

        oss << " with linespoints";

        ++entry;
    }

    oss << std::endl;

    g_lines.replace(ln, eln, indent, oss.str(), plot_type);
}

//! Process # PLOT commands
static inline void
process_plot(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("--> " << sql.num_rows() << " rows");

    // write a header to the datafile containing the query
    std::ostream& df = *g_datafile;

    df << std::string(80, '#') << std::endl
       << "# PLOT " << cmdline << std::endl
       << '#' << std::endl;

    // write result data rows
    while (sql.step())
    {
        for (unsigned int col = 0; col < sql.num_cols(); ++col)
        {
            if (col != 0) df << '\t';
            df << sql.text(col);
        }
        df << std::endl;
    }

    // append plot line to gnuplot
    std::vector<GnuplotDataset> datasets(1);
    datasets[0].index = g_dataindex;

    // finish index in datafile
    df << std::endl << std::endl;
    ++g_dataindex;

    gnuplot_plot_rewrite(ln, indent, datasets, "PLOT");
}

//! Process # MULTIPLOT commands
static inline void
process_multiplot(size_t ln, size_t indent, const std::string& cmdline)
{
    // extract MULTIPLOT columns
    static const boost::regex
        re_multiplot("MULTIPLOT\\(([^)]+)\\) (SELECT .+)");
    boost::smatch rm_multiplot;

    if (!boost::regex_match(cmdline, rm_multiplot, re_multiplot))
        OUT_THROW("MULTIPLOT() requires group column list.");

    std::string multiplot = rm_multiplot[1].str();
    std::string query = rm_multiplot[2].str();

    query = replace_all(query, "MULTIPLOT", multiplot);

    std::vector<std::string> groupfields = split(multiplot, ',');
    std::for_each(groupfields.begin(), groupfields.end(), trim_inplace_ws);

    // execute query
    SqlQuery sql(query);
    OUT("--> " << sql.num_rows() << " rows");

    // read column names
    sql.read_colmap();

    // check for existing x and y columns.
    if (!sql.exist_col("x"))
        OUT_THROW("MULTIPLOT failed: result contains no 'x' column.");

    if (!sql.exist_col("y"))
        OUT_THROW("MULTIPLOT failed: result contains no 'y' column.");

    unsigned int colx = sql.find_col("x"), coly = sql.find_col("y");

    // check existance of group fields and save ids
    std::vector<int> groupcols;
    for (std::vector<std::string>::const_iterator gi = groupfields.begin();
         gi != groupfields.end(); ++gi)
    {
        if (!sql.exist_col(*gi))
        {
            OUT_THROW("MULTIPLOT failed: result contains no '" << *gi <<
                      "' column, which is a MULTIPLOT group field.");
        }
        groupcols.push_back(sql.find_col(*gi));
    }

    // write a header to the datafile containing the query
    std::ostream& df = *g_datafile;

    df << std::string(80, '#') << std::endl
       << "# " << cmdline << std::endl
       << '#' << std::endl;

    // collect coordinates groups
    std::vector<GnuplotDataset> datasets;

    {
        std::vector<std::string> lastgroup;

        while (sql.step())
        {
            // collect groupfields for this row
            std::vector<std::string> rowgroup (groupcols.size());

            for (size_t i = 0; i < groupcols.size(); ++i)
                rowgroup[i] = sql.text(groupcols[i]);

            if (sql.curr_row() == 0 || lastgroup != rowgroup)
            {
                // group fields mismatch (or first row) -> start new group
                if (sql.curr_row() != 0) {
                    df << std::endl << std::endl;
                    ++g_dataindex;
                }

                lastgroup  = rowgroup;

                // store group's legend string
                std::ostringstream os;
                for (size_t i = 0; i < groupcols.size(); ++i) {
                    if (i != 0) os << ',';
                    os << groupfields[i] << '=' << rowgroup[i];
                }
                datasets.push_back(GnuplotDataset());
                datasets.back().index = g_dataindex;
                datasets.back().title = os.str();

                df << "# index " << g_dataindex << ' ' << os.str() << std::endl;
            }

            // group fields match with last row -> append coordinates.
            df << sql.text(colx) << '\t'
               << sql.text(coly) << std::endl;
        }

        // finish last plot
        df << std::endl << std::endl;
        ++g_dataindex;
    }

    gnuplot_plot_rewrite(ln, indent, datasets, "MULTIPLOT");
}

static inline
std::string maybe_quote(const char* str)
{
    char *endptr;
    strtod(str, &endptr);

    if (endptr && *endptr == 0) // fully parsed
        return str;
    else // needs quoting
        return std::string("'") + str + "'";
}

//! Process # MACRO commands
static inline void
process_macro(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("--> " << sql.num_rows() << " rows");

    if (sql.num_rows() != 1)
        OUT_THROW("MACRO did not return exactly one row");

    sql.step();

    // write each column as macro value
    std::ostringstream oss;

    for (unsigned int col = 0; col < sql.num_cols(); ++col)
    {
        oss << sql.col_name(col) << " = "
            << maybe_quote( sql.text(col) ) << std::endl;
    }

    // scan following lines for macro defintions
    static const boost::regex re_macro("[^=]+ = .*");

    size_t eln = ln;
    while (eln < g_lines.size() &&
           boost::regex_match(g_lines[eln], re_macro))
    {
        ++eln;
    }

    g_lines.replace(ln, eln, indent, oss.str(), "MACRO");
}

//! process line-based file in place
void process()
{
    // iterate over all lines
    for (size_t ln = 0; ln < g_lines.size();)
    {
        // try to collect an aligned comment block
        int indent = is_comment_line(g_lines[ln]);
        if (indent < 0) {
            ++ln;
            continue;
        }

        std::string cmdline = g_lines[ln++].substr(indent+1);

        // collect lines while they are at the same indentation level
        while ( ln < g_lines.size() &&
                is_comment_line(g_lines[ln]) == indent )
        {
            cmdline += g_lines[ln++].substr(indent+1);
        }

        cmdline = trim(cmdline);

        // extract first word
        std::string::size_type space_pos =
            cmdline.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-_");
        std::string first_word = cmdline.substr(0, space_pos);

        if (first_word == "SQL")
        {
            OUT("# " << cmdline);
            process_sql(ln, indent, cmdline.substr(space_pos+1));
        }
        else if (first_word == "IMPORT-DATA")
        {
            OUT("# " << cmdline);
            process_importdata(ln, indent, cmdline);
        }
        else if (first_word == "PLOT")
        {
            OUT("# " << cmdline);
            process_plot(ln, indent, cmdline.substr(space_pos+1));
        }
        else if (first_word == "MULTIPLOT")
        {
            OUT("# " << cmdline);
            process_multiplot(ln, indent, cmdline);
        }
        else if (first_word == "MACRO")
        {
            OUT("# " << cmdline);
            process_macro(ln, indent, cmdline.substr(space_pos+1));
        }
    }
}

//! process a stream
static inline void
process_stream(const std::string& filename, std::istream& is)
{
    // read complete Gnuplot file line-wise
    g_lines.read_stream(is);

    // construct output data file
    g_datafilename = filename;
    std::string::size_type dotpos = g_datafilename.rfind('.');
    if (dotpos != std::string::npos)
        g_datafilename = g_datafilename.substr(0, dotpos);
    g_datafilename += "-data.txt";

    // open output data file
    if (!gopt_check_output)
    {
        g_datafile = new std::ofstream(g_datafilename.c_str());
        if (!g_datafile->good()) {
            OUT("Fatal error opening datafile " << g_datafilename << ": " << strerror(errno));
            delete g_datafile;
            return;
        }
    }
    else
    {
        // open temporary data file
        g_datafile = new std::ostringstream();
    }
    g_dataindex = 0;

    // process lines in place
    process();

    // verify processed output against file
    if (gopt_check_output)
    {
        std::ifstream in(g_datafilename.c_str());
        if (!in.good()) {
            OUT("Error reading " << g_datafilename << ": " << strerror(errno));
            exit(EXIT_FAILURE);
        }
        std::string checkdata = read_stream(in);

        std::ostringstream* oss = (std::ostringstream*)g_datafile;

        if (checkdata != oss->str())
            OUT_THROW("Mismatch to expected output data file " << g_datafilename);
        else
            OUT("Good match to expected output data file " << g_datafilename);
    }

    delete g_datafile;

    OUT("--- Finished processing " << filename << " successfully.");
}

//! print command line usage
static inline int
print_usage(const std::string& progname)
{
    OUT("Usage: " << progname << " [options] [files...]" << std::endl <<
        std::endl <<
        "Options: " << std::endl <<
        "  -v         Increase verbosity." << std::endl <<
        "  -o <file>  Output all processed files to this stream." << std::endl <<
        "  -C         Verify that -o output file matches processed data (for tests)." << std::endl <<
        std::endl);

    return EXIT_FAILURE;
}

//! process Gnuplot, main function
static inline int
sp_gnuplot(int argc, char* argv[])
{
    // parse command line parameters
    int opt;

    //! output file name
    std::string opt_outputfile;

    while ((opt = getopt(argc, argv, "vo:C")) != -1) {
        switch (opt) {
        case 'v':
            gopt_verbose++;
            break;
        case 'o':
            opt_outputfile = optarg;
            break;
        case 'C':
            gopt_check_output = true;
            break;
        case 'h': default:
            return print_usage(argv[0]);
        }
    }

    // make connection to the database
    g_pg = PQconnectdb("");

    // check to see that the backend connection was successfully made
    if (PQstatus(g_pg) != CONNECTION_OK)
        OUT_THROW("Connection to database failed: " << PQerrorMessage(g_pg));

    // open output file or string stream
    std::ostream* output = NULL;
    if (gopt_check_output)
    {
        if (!opt_outputfile.size())
            OUT_THROW("Error: checking output requires and output filename.");

        output = new std::ostringstream;
    }
    else if (opt_outputfile.size())
    {
        output = new std::ofstream(opt_outputfile.c_str());

        if (!output->good())
            OUT_THROW("Error opening output stream: " << strerror(errno));
    }

    // process file commandline arguments
    if (optind < argc)
    {
        while (optind < argc)
        {
            const char* filename = argv[optind];

            std::ifstream in(filename);
            if (!in.good()) {
                OUT_THROW("Error reading " << filename << ": " << strerror(errno));
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
                    if (!out.good())
                        OUT_THROW("Error writing " << filename << ": " << strerror(errno));

                    g_lines.write_stream(out);
                    if (!out.good())
                        OUT_THROW("Error writing " << filename << ": " << strerror(errno));
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
    if (gopt_check_output)
    {
        std::ifstream in(opt_outputfile.c_str());
        if (!in.good()) {
            OUT("Error reading " << opt_outputfile << ": " << strerror(errno));
            return EXIT_FAILURE;
        }
        std::string checkdata = read_stream(in);

        assert(output);
        std::ostringstream* oss = (std::ostringstream*)output;

        if (checkdata != oss->str())
            OUT_THROW("Mismatch to expected output file " << opt_outputfile);
        else
            OUT("Good match to expected output file " << opt_outputfile);
    }

    if (output) delete output;

    PQfinish(g_pg);
    return EXIT_SUCCESS;
}

//! main(), yay.
int main(int argc, char* argv[])
{
    try {
        return sp_gnuplot(argc, argv);
    }
    catch (std::runtime_error& e)
    {
        OUT(e.what());
        return EXIT_FAILURE;
    }
}
