/******************************************************************************
 * src/gnuplot.cpp
 *
 * Process embedded SQL plot instructions in Gnuplot files.
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

#include <boost/regex.hpp>

#include "common.h"
#include "strtools.h"
#include "sql.h"
#include "textlines.h"
#include "importdata.h"

class SpGnuplot
{
public:
    //! processed line data
    TextLines&  m_lines;

    //! comment character
    static const char comment_char = '#';

    // *** current gnuplot datafile ***

    std::ostream* m_datafile;
    std::string m_datafilename;
    unsigned int m_dataindex;

    //! scan for next comment line with given prefix
    inline ssize_t
    scan_lines_for_comment(size_t ln, const std::string& cprefix)
    {
        return m_lines.scan_for_comment<comment_char>(ln, cprefix);
    }

    //! Process # SQL commands
    void sql(size_t ln, size_t indent, const std::string& cmdline);

    //! Process # IMPORT-DATA commands
    void importdata(size_t ln, size_t indent, const std::string& cmdline);

    //! Struct to rewrite Gnuplot "plot" directives with new datafile/index pairs
    struct Dataset
    {
        unsigned int index;
        std::string title;
    };

    //! Helper to rewrite Gnuplot "plot" directives with new datafile/index pairs
    void plot_rewrite(size_t ln, size_t indent,
                      const std::vector<Dataset>& datasets,
                      const char* plot_type);

    //! Process # PLOT commands
    void plot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process # MULTIPLOT commands
    void multiplot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process # MACRO commands
    void macro(size_t ln, size_t indent, const std::string& cmdline);

    //! Process TextLines
    void process();

    //! Process Textlines
    SpGnuplot(const std::string& filename, TextLines& lines);
};

//! Process # SQL commands
void SpGnuplot::sql(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    SqlQuery sql = g_db->query(cmdline);
    OUT("SQL command successful.");
}

//! Process # IMPORT-DATA commands
void SpGnuplot::importdata(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
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
void SpGnuplot::plot_rewrite(size_t ln, size_t indent,
                             const std::vector<Dataset>& datasets,
                             const char* plot_type)
{
    std::ostringstream oss;

    // check whether line contains an "plot" command
    static const boost::regex re_plot("[[:blank:]]*plot.*\\\\[[:blank:]]*");

    if (ln >= m_lines.size() ||
        !boost::regex_match(m_lines[ln], re_plot))
    {
        // no "plot" command: construct default version from scratch

        if (datasets.size())
            oss << "plot";

        for (size_t i = 0; i < datasets.size(); ++i)
        {
            if (i != 0) oss << ',';
            oss << " \\" << std::endl
                << "    '" << m_datafilename << "' index " << datasets[i].index;

            if (datasets[i].title.size())
                oss << " title \"" << datasets[i].title << '"';

            oss << " with linespoints";
        }

        if (datasets.size())
            oss << std::endl;

        m_lines.replace(ln, ln, indent, oss.str(), plot_type);
        return;
    }

    // scan following lines for plot descriptions
    static const boost::regex re_line("[[:blank:]]*'[^']+' index [0-9]+( title \"[^\"]*\")?( .*?)(, \\\\)?[[:blank:]]*");
    boost::smatch rm;

    if (datasets.size())
        oss << "plot";

    size_t eln = ln+1;
    size_t entry = 0; // dataset entry

    while (eln < m_lines.size() &&
           boost::regex_match(m_lines[eln], rm, re_line))
    {
        ++eln;

        // copy properties to new plot line
        if (entry < datasets.size())
        {
            if (entry != 0) oss << ',';
            oss << " \\" << std::endl
                << "    '" << m_datafilename << "' index " << datasets[entry].index;

            // if dataset contains a title, add it
            if (datasets[entry].title.size())
                oss << " title \"" << datasets[entry].title << '"';

            // output extended properties
            oss << rm[2];

            ++entry;

            // break if no \ was found at the end
            if (rm[3].length() == 0) break;
        }
        else
        {
            // gobble additional plot line
        }
    }

    // append missing plot descriptions
    while (entry < datasets.size())
    {
        if (entry != 0) oss << ',';
        oss << " \\" << std::endl
            << "    '" << m_datafilename << "' index " << datasets[entry].index;

        if (datasets[entry].title.size())
            oss << " title \"" << datasets[entry].title << '"';

        oss << " with linespoints";

        ++entry;
    }

    if (datasets.size())
        oss << std::endl;

    m_lines.replace(ln, eln, indent, oss.str(), plot_type);
}

//! Process # PLOT commands
void SpGnuplot::plot(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql = g_db->query(cmdline);

    // write a header to the datafile containing the query
    std::ostream& df = *m_datafile;

    df << std::string(80, '#') << std::endl
       << "# PLOT " << cmdline << std::endl
       << '#' << std::endl;

    // write result data rows
    while (sql->step())
    {
        for (unsigned int col = 0; col < sql->num_cols(); ++col)
        {
            if (col != 0) df << '\t';
            df << sql->text(col);
        }
        df << std::endl;
    }

    // append plot line to gnuplot
    std::vector<Dataset> datasets(1);
    datasets[0].index = m_dataindex;

    // finish index in datafile
    df << std::endl << std::endl;
    ++m_dataindex;

    plot_rewrite(ln, indent, datasets, "PLOT");
}

//! Process # MULTIPLOT commands
void SpGnuplot::multiplot(size_t ln, size_t indent, const std::string& cmdline)
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
    SqlQuery sql = g_db->query(query);

    std::vector<Dataset> datasets;

    // read column names
    sql->read_colmap();

    // check for existing x and y columns.
    if (!sql->exist_col("x"))
        OUT_THROW("MULTIPLOT failed: result contains no 'x' column.");

    if (!sql->exist_col("y"))
        OUT_THROW("MULTIPLOT failed: result contains no 'y' column.");

    unsigned int colx = sql->find_col("x"), coly = sql->find_col("y");

    // check existance of group fields and save ids
    std::vector<int> groupcols;
    for (std::vector<std::string>::const_iterator gi = groupfields.begin();
         gi != groupfields.end(); ++gi)
    {
        if (!sql->exist_col(*gi))
        {
            OUT_THROW("MULTIPLOT failed: result contains no '" << *gi <<
                      "' column, which is a MULTIPLOT group field.");
        }
        groupcols.push_back(sql->find_col(*gi));
    }

    // write a header to the datafile containing the query
    std::ostream& df = *m_datafile;

    df << std::string(80, '#') << std::endl
       << "# " << cmdline << std::endl
       << '#' << std::endl;

    // collect coordinates groups
    {
        std::vector<std::string> lastgroup;

        while (sql->step())
        {
            // collect groupfields for this row
            std::vector<std::string> rowgroup (groupcols.size());

            for (size_t i = 0; i < groupcols.size(); ++i)
                rowgroup[i] = sql->text(groupcols[i]);

            if (sql->current_row() == 0 || lastgroup != rowgroup)
            {
                // group fields mismatch (or first row) -> start new group
                if (sql->current_row() != 0) {
                    df << std::endl << std::endl;
                    ++m_dataindex;
                }

                lastgroup  = rowgroup;

                // store group's legend string
                std::ostringstream os;
                for (size_t i = 0; i < groupcols.size(); ++i) {
                    if (i != 0) os << ',';
                    os << groupfields[i] << '=' << rowgroup[i];
                }
                datasets.push_back(Dataset());
                datasets.back().index = m_dataindex;
                datasets.back().title = os.str();

                df << "# index " << m_dataindex << ' ' << os.str() << std::endl;
            }

            // group fields match with last row -> append coordinates.
            df << sql->text(colx) << '\t'
               << sql->text(coly) << std::endl;
        }

        // finish last plot
        df << std::endl << std::endl;
        ++m_dataindex;
    }

    plot_rewrite(ln, indent, datasets, "MULTIPLOT");
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
void SpGnuplot::macro(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql = g_db->query(cmdline);

    sql->step();

    // write each column as macro value
    std::ostringstream oss;

    for (unsigned int col = 0; col < sql->num_cols(); ++col)
    {
        oss << sql->col_name(col) << " = "
            << maybe_quote( sql->text(col) ) << std::endl;
    }

    // scan following lines for macro defintions
    static const boost::regex re_macro("[^=]+ = .*");

    size_t eln = ln;
    while (eln < m_lines.size() &&
           boost::regex_match(m_lines[eln], re_macro))
    {
        ++eln;
    }

    m_lines.replace(ln, eln, indent, oss.str(), "MACRO");
}

//! process line-based file in place
void SpGnuplot::process()
{
    // iterate over all lines
    for (size_t ln = 0; ln < m_lines.size();)
    {
        // collect command from comment lines
        std::string cmd;
        size_t indent;

        if (!m_lines.collect_comment<comment_char>(ln, cmd, indent))
            continue;

        // extract first word
        std::string::size_type space_pos =
            cmd.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-_");
        std::string first_word = cmd.substr(0, space_pos);

        if (first_word == "SQL")
        {
            OUT("# " << cmd);
            sql(ln, indent, cmd.substr(space_pos+1));
        }
        else if (first_word == "IMPORT-DATA")
        {
            OUT("# " << cmd);
            importdata(ln, indent, cmd);
        }
        else if (first_word == "PLOT")
        {
            OUT("# " << cmd);
            plot(ln, indent, cmd.substr(space_pos+1));
        }
        else if (first_word == "MULTIPLOT")
        {
            OUT("# " << cmd);
            multiplot(ln, indent, cmd);
        }
        else if (first_word == "MACRO")
        {
            OUT("# " << cmd);
            macro(ln, indent, cmd.substr(space_pos+1));
        }
        else
        {
            if (first_word.size() >= 4 && first_word[0] != '-')
                OUT("? maybe unknown keyword " << first_word);
        }
    }
}

//! process a stream
SpGnuplot::SpGnuplot(const std::string& filename, TextLines& lines)
    : m_lines(lines)
{
    // construct output data file
    m_datafilename = filename;
    std::string::size_type dotpos = m_datafilename.rfind('.');
    if (dotpos != std::string::npos)
        m_datafilename = m_datafilename.substr(0, dotpos);
    m_datafilename += "-data.txt";

    // open output data file
    if (!gopt_check_output)
    {
        m_datafile = new std::ofstream(m_datafilename.c_str());
        if (!m_datafile->good()) {
            OUT("Fatal error opening datafile " << m_datafilename << ": " << strerror(errno));
            delete m_datafile;
            return;
        }
    }
    else
    {
        // open temporary data file
        m_datafile = new std::ostringstream();
    }
    m_dataindex = 0;

    // process lines in place
    process();

    // verify processed output against file
    if (gopt_check_output)
    {
        std::ifstream in(m_datafilename.c_str());
        if (!in.good()) {
            OUT("Error reading " << m_datafilename << ": " << strerror(errno));
            exit(EXIT_FAILURE);
        }
        std::string checkdata = read_stream(in);

        std::ostringstream* oss = (std::ostringstream*)m_datafile;

        if (checkdata != oss->str())
            OUT_THROW("Mismatch to expected output data file " << m_datafilename);
        else
            OUT("Good match to expected output data file " << m_datafilename);
    }

    delete m_datafile;
}

//! Process Gnuplot file
void sp_gnuplot(const std::string& filename, TextLines& lines)
{
    SpGnuplot sp(filename, lines);
}
