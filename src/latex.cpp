/******************************************************************************
 * src/latex.cpp
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

#include <boost/regex.hpp>

#include "common.h"
#include "strtools.h"
#include "pgsql.h"
#include "textlines.h"
#include "importdata.h"

class SpLatex
{
public:

    //! processed line data
    TextLines&  m_lines;

    //! scan line for LaTeX comment, returns index of % or -1 if the line is
    //! not a plain comment
    static inline int
    is_comment_line(const std::string& line)
    {
        int i = 0;
        while (isblank(line[i])) ++i;
        return (line[i] == '%') ? i : -1;
    }

    //! scan for next comment line with given prefix
    inline ssize_t
    scan_lines_for_comment(size_t ln, const std::string& cprefix)
    {
        return m_lines.scan_for_comment<is_comment_line>(ln, cprefix);
    }

    //! Process % SQL commands
    void sql(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % IMPORT-DATA commands
    void importdata(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % TEXTTABLE commands
    void texttable(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % PLOT commands
    void plot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % MULTIPLOT commands
    void multiplot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % TABULAR commands
    void tabular(size_t ln, size_t indent, const std::string& cmdline);

    //! Process Textlines
    SpLatex(TextLines& lines);
};

//! Process % SQL commands
void SpLatex::sql(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("SQL command successful.");
}

//! Process % IMPORT-DATA commands
void SpLatex::importdata(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
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

//! Process % TEXTTABLE commands
void SpLatex::texttable(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("--> " << sql.num_rows() << " rows");

    // format result as a text table
    std::string output = sql.format_texttable();

    output += shorten("% END TEXTTABLE " + cmdline) + "\n";

    // find following "% END TEXTTABLE" and replace enclosing lines
    ssize_t eln = scan_lines_for_comment(ln, "END TEXTTABLE");

    if (eln < 0) {
        m_lines.replace(ln, ln, indent, output, "TEXTTABLE");
    }
    else {
        m_lines.replace(ln, eln+1, indent, output, "TEXTTABLE");
    }
}

//! Process % PLOT commands
void SpLatex::plot(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql(cmdline);
    OUT("--> " << sql.num_rows() << " rows");

    std::ostringstream oss;
    while (sql.step())
    {
        oss << " (";
        for (unsigned int col = 0; col < sql.num_cols(); ++col)
        {
            if (col != 0) oss << ',';
            oss << sql.text(col);
        }
        oss << ')';
    }

    // check whether line contains an \addplot command
    static const boost::regex
        re_addplot("[[:blank:]]*(\\\\addplot.*coordinates \\{)[^}]+(\\};.*)");
    boost::smatch rm;

    if (ln < m_lines.size() &&
        boost::regex_match(m_lines[ln], rm, re_addplot))
    {
        std::string output = rm[1].str() + oss.str() + " " + rm[2].str();
        m_lines.replace(ln, ln+1, indent, output, "PLOT");
    }
    else
    {
        std::string output = "\\addplot coordinates {" + oss.str() + " };";
        m_lines.replace(ln, ln, indent, output, "PLOT");
    }
}

//! Process % MULTIPLOT commands
void SpLatex::multiplot(size_t ln, size_t indent, const std::string& cmdline)
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

    // collect coordinates {...} clause groups
    std::vector<std::string> coordlist;
    std::vector<std::string> legendlist;

    {
        std::vector<std::string> lastgroup;
        std::ostringstream coord;

        for (unsigned int row = 0; row < sql.num_rows(); ++row)
        {
            sql.step();

            // collect groupfields for this row
            std::vector<std::string> rowgroup (groupcols.size());

            for (size_t i = 0; i < groupcols.size(); ++i)
                rowgroup[i] = sql.text(groupcols[i]);

            if (row == 0 || lastgroup != rowgroup)
            {
                // group fields mismatch (or first row) -> start new group
                if (row != 0) {
                    coordlist.push_back(coord.str());
                    coord.str("");
                }

                lastgroup  = rowgroup;

                // store group's legend string
                std::ostringstream os;
                for (size_t i = 0; i < groupcols.size(); ++i) {
                    if (i != 0) os << ',';
                    os << groupfields[i] << '=' << rowgroup[i];
                }
                legendlist.push_back(os.str());
            }

            // group fields match with last row -> append coordinates.
            coord << " (" << sql.text(colx)
                  <<  ',' << sql.text(coly)
                  <<  ')';
        }

        // store last coordates group
        coordlist.push_back(coord.str());
    }

    assert(coordlist.size() == legendlist.size());

    for (size_t i = 0; i < coordlist.size(); ++i)
    {
        OUTC(gopt_verbose >= 1, "coordinates {" << coordlist[i] << " }");
        OUTC(gopt_verbose >= 1, "legend {" << legendlist[i] << " }");
    }

    // create output text, merging in existing styles and suffixes
    std::ostringstream out;
    size_t eln = ln;
    size_t entry = 0; // coordinates/legend entry

    static const boost::regex
        re_addplot("[[:blank:]]*(\\\\addplot.*coordinates \\{)[^}]+(\\};.*)");
    static const boost::regex
        re_legend("[[:blank:]]*(\\\\addlegendentry\\{).*(\\};.*)");

    boost::smatch rm;

    // check whether line contains an \addplot command
    while (eln < m_lines.size() &&
           boost::regex_match(m_lines[eln], rm, re_addplot))
    {
        // copy styles from \addplot line
        if (entry < coordlist.size())
        {
            out << rm[1] << coordlist[entry] << " " << rm[2] << std::endl;

            // check following \addlegendentry
            if (eln+1 < m_lines.size() &&
                boost::regex_match(m_lines[eln+1], rm, re_legend))
            {
                // copy styles
                out << rm[1] << legendlist[entry] << rm[2] << std::endl;
                ++eln;
            }
            else
            {
                // add missing \addlegendentry
                out << "\\addlegendentry{" << legendlist[entry]
                    << "};" << std::endl;
            }

            ++entry;
        }
        else
        {
            // remove \addplot and following \addlegendentry as well.
            if (eln+1 < m_lines.size() &&
                boost::regex_match(m_lines[eln+1], re_legend))
            {
                // skip thus remove \addlegendentry
                ++eln;
            }
        }

        ++eln;
    }

    // append missing \addplot / \addlegendentry pairs
    while (entry < coordlist.size())
    {
        out << "\\addplot coordinates {" << coordlist[entry]
            << " };" << std::endl;

        out << "\\addlegendentry{" << legendlist[entry]
            << "};" << std::endl;

        ++entry;
    }

    m_lines.replace(ln, eln, indent, out.str(), "MULTIPLOT");
}

//! Process % TABULAR commands
void SpLatex::tabular(size_t ln, size_t indent, const std::string& cmdline)
{
    const std::string& query = cmdline;

    // execute query
    SqlQuery sql(query);
    OUT("--> " << sql.num_rows() << " rows");

    sql.read_complete();

    // calculate width of columns data
    std::vector<size_t> cwidth(sql.num_cols(), 0);

    for (unsigned int i = 0; i < sql.num_rows(); ++i)
    {
        for (unsigned int j = 0; j < sql.num_cols(); ++j)
        {
            cwidth[j] = std::max(cwidth[j], strlen( sql.text(i, j) ));
        }
    }

    // generate output
    std::vector<std::string> tlines;
    for (unsigned int i = 0; i < sql.num_rows(); ++i)
    {
        std::ostringstream out;
        for (unsigned j = 0; j < sql.num_cols(); ++j)
        {
            if (j != 0) out << " & ";
            out << std::setw(cwidth[j]) << sql.text(i,j);
        }
        out << " \\\\";
        tlines.push_back(out.str());
    }

    // scan lines forward till next comment directive
    size_t eln = ln;
    while (eln < m_lines.size() && is_comment_line(m_lines[eln]) < 0)
        ++eln;

    static const boost::regex
        re_endtabular("[[:blank:]]*% END TABULAR .*");

    if (eln < m_lines.size() &&
        boost::regex_match(m_lines[eln], re_endtabular))
    {
        // found END TABULAR
        size_t rln = ln;
        size_t entry = 0;

        static const boost::regex re_tabular(".*\\\\(.*)");
        boost::smatch rm;

        // iterate over tabular lines, copy styles to replacement
        while (entry < tlines.size() && rln < eln &&
               boost::regex_match(m_lines[rln], rm, re_tabular))
        {
            tlines[entry++] += rm[1];
            ++rln;
        }

        tlines.push_back(shorten("% END TABULAR " + query));
        m_lines.replace(ln, eln+1, indent, tlines, "TABULAR");
    }
    else
    {
        // could not find END TABULAR: insert whole table.
        tlines.push_back(shorten("% END TABULAR " + query));
        m_lines.replace(ln, ln, indent, tlines, "TABULAR");
    }
}

//! process line-based file in place
SpLatex::SpLatex(TextLines& lines)
    : m_lines(lines)
{
    // iterate over all lines
    for (size_t ln = 0; ln < m_lines.size();)
    {
        // try to collect an aligned comment block
        int indent = is_comment_line(m_lines[ln]);
        if (indent < 0) {
            ++ln;
            continue;
        }

        std::string cmdline = m_lines[ln++].substr(indent+1);

        // collect lines while they are at the same indentation level
        while ( ln < m_lines.size() &&
                is_comment_line(m_lines[ln]) == indent )
        {
            cmdline += m_lines[ln++].substr(indent+1);
        }

        cmdline = trim(cmdline);

        // extract first word
        std::string::size_type space_pos =
            cmdline.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-_");
        std::string first_word = cmdline.substr(0, space_pos);

        if (first_word == "SQL")
        {
            OUT("% " << cmdline);
            sql(ln, indent, cmdline.substr(space_pos+1));
        }
        else if (first_word == "IMPORT-DATA")
        {
            OUT("% " << cmdline);
            importdata(ln, indent, cmdline);
        }
        else if (first_word == "TEXTTABLE")
        {
            OUT("% " << cmdline);
            texttable(ln, indent, cmdline.substr(space_pos+1));
        }
        else if (first_word == "PLOT")
        {
            OUT("% " << cmdline);
            plot(ln, indent, cmdline.substr(space_pos+1));
        }
        else if (first_word == "MULTIPLOT")
        {
            OUT("% " << cmdline);
            multiplot(ln, indent, cmdline);
        }
        else if (first_word == "TABULAR")
        {
            OUT("% " << cmdline);
            tabular(ln, indent, cmdline.substr(space_pos+1));
        }
    }
}

//! Process LaTeX file
void sp_latex(const std::string& /* filename */, TextLines& lines)
{
    SpLatex sp(lines);
}
