/******************************************************************************
 * src/latex.cpp
 *
 * Process embedded SQL plot instructions in LaTeX files.
 *
 ******************************************************************************
 * Copyright (C) 2013-2016 Timo Bingmann <tb@panthema.net>
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
#include "reformat.h"

class SpLatex
{
public:
    //! processed line data
    TextLines&  m_lines;

    //! comment character
    static const char comment_char = '%';

    //! scan line for LaTeX comment, returns index of % or -1 if the line is
    //! not a plain comment
    inline int
    is_comment_line(size_t ln)
    {
        return m_lines.is_comment_line<comment_char>(ln);
    }

    //! scan for next comment line with given prefix
    inline ssize_t
    scan_lines_for_comment(size_t ln, const std::string& cprefix)
    {
        return m_lines.scan_for_comment<comment_char>(ln, cprefix);
    }

    //! Process % SQL commands
    void sql(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % IMPORT-DATA commands
    int importdata(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % CONNECT command
    bool connect(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % TEXTTABLE commands
    void texttable(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % PLOT commands
    void plot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % MULTIPLOT commands
    void multiplot(size_t ln, size_t indent, const std::string& cmdline);

    //! Process % TABULAR commands
    void tabular(size_t ln,
                 size_t indent,
                 const std::string& cmdline,
                 const std::string& op_name,
                 const std::string& separator,
                 const std::string& endline,
                 const std::string& gobble_regex);

    //! Process % DEFMACRO commands
    void defmacro(size_t ln, size_t indent, const std::string& cmdline);

    //! Process Textlines
    SpLatex(TextLines& lines);
};

//! Process % SQL commands
void SpLatex::sql(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    SqlQuery sql = g_db->query(cmdline);
    OUT("SQL command successful.");
}

//! Process % IMPORT-DATA commands
int SpLatex::importdata(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    // split argument at whitespaces
    std::vector<std::string> args = split_ws(cmdline);

    // convert std::strings to char*
    char* argv[args.size() + 1];

    for (size_t i = 0; i < args.size(); ++i)
        argv[i] = (char*)args[i].c_str();

    argv[args.size()] = NULL;

    return ImportData(true).main(args.size(), argv);
}

//! Process % CONNECT command
bool SpLatex::connect(size_t /* ln */, size_t /* indent */, const std::string& cmdline)
{
    return g_db_connect(cmdline);
}

//! Process % TEXTTABLE commands
void SpLatex::texttable(size_t ln, size_t indent, const std::string& cmdline)
{
    SqlQuery sql = g_db->query(cmdline);

    // format result as a text table
    std::string output = sql->format_texttable();

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
    SqlQuery sql = g_db->query(cmdline);

    std::ostringstream oss;
    while (sql->step())
    {
        oss << " (";
        for (unsigned int col = 0; col < sql->num_cols(); ++col)
        {
            if (col != 0) oss << ',';
            oss << str_reduce(sql->text(col));
        }
        oss << ')';
    }

    // check whether line contains an \addplot command
    static const boost::regex
        re_addplot("[[:blank:]]*(\\\\addplot.*coordinates \\{)[^}]+(\\}[^;]*;.*)");
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
        re_multiplot("MULTIPLOT\\(([^)]+)\\) (.+)");
    boost::smatch rm_multiplot;

    if (!boost::regex_match(cmdline, rm_multiplot, re_multiplot))
        OUT_THROW("MULTIPLOT() requires group column list.");

    std::string multiplot = rm_multiplot[1].str();
    std::string query = rm_multiplot[2].str();

    std::vector<std::string> groupfields = split(multiplot, ',');
    std::for_each(groupfields.begin(), groupfields.end(), trim_inplace_ws);

    bool title_mark = false;
    bool ptitle_mark = false;
    bool xerr = false, yerr = false;

    if (!groupfields.empty() && is_suffix(groupfields.back(), "|title")) {
        // remove |title from multiplot string
        groupfields.back().resize(groupfields.back().size() - 6);
        multiplot.resize(multiplot.size() - 6);
        title_mark = true;
    }
    else if (!groupfields.empty() && is_suffix(groupfields.back(), "|ptitle")) {
        // remove |ptitle from multiplot string
        groupfields.back().resize(groupfields.back().size() - 7);
        multiplot.resize(multiplot.size() - 7);
        ptitle_mark = true;
    }

    // execute query
    query = replace_all(query, "MULTIPLOT", multiplot);
    SqlQuery sql = g_db->query(query);

    // read column names
    sql->read_colmap();

    // check for existing x and y columns.
    if (!sql->exist_col("x"))
        OUT_THROW("MULTIPLOT failed: result contains no 'x' column.");

    if (!sql->exist_col("y"))
        OUT_THROW("MULTIPLOT failed: result contains no 'y' column.");

    if (sql->exist_col("xerr"))
        xerr = true;

    if (sql->exist_col("yerr"))
        yerr = true;

    if (title_mark && !sql->exist_col("title"))
        OUT_THROW("MULTIPLOT failed: title mark set but result contains no 'title' column.");

    if (ptitle_mark && !sql->exist_col("ptitle"))
        OUT_THROW("MULTIPLOT failed: ptitle mark set but result contains no 'ptitle' column.");

    unsigned int col_x = sql->find_col("x"), col_y = sql->find_col("y"),
        col_xerr = xerr ? sql->find_col("xerr") : -1,
        col_yerr = yerr ? sql->find_col("yerr") : -1;

    unsigned int col_title = 0;
    if (title_mark)
        col_title = sql->find_col("title");
    if (ptitle_mark)
        col_title = sql->find_col("ptitle");

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

    // collect coordinates {...} clause groups
    std::vector<std::string> coordlist;
    std::vector<std::string> legendlist;

    {
        std::vector<std::string> lastgroup;
        std::ostringstream coord;

        while (sql->step())
        {
            unsigned int row = sql->current_row();

            if (sql->isNULL(col_x)) {
                OUT("MULTIPLOT warning: 'x' is NULL in row " << row << ".");
                continue;
            }
            if (sql->isNULL(col_y)) {
                OUT("MULTIPLOT warning: 'y' is NULL in row " << row << ".");
                continue;
            }

            // collect groupfields for this row
            std::vector<std::string> rowgroup (groupcols.size());

            for (size_t i = 0; i < groupcols.size(); ++i)
                rowgroup[i] = sql->text(groupcols[i]);

            if (row == 0 || lastgroup != rowgroup)
            {
                // group fields mismatch (or first row) -> start new group
                if (row != 0) {
                    coordlist.push_back(coord.str());
                    coord.str("");
                }

                lastgroup  = rowgroup;

                if (title_mark) {
                    legendlist.push_back(escape_latex(sql->text(col_title)));
                }
                else if (ptitle_mark) {
                    legendlist.push_back(sql->text(col_title));
                }
                else {
                    // store group's legend string
                    std::ostringstream os;
                    for (size_t i = 0; i < groupcols.size(); ++i) {
                        if (i != 0) os << ',';
                        os << escape_latex(groupfields[i]) << '='
                           << escape_latex(rowgroup[i]);
                    }
                    legendlist.push_back(os.str());
                }
            }

            // group fields match with last row -> append coordinates.
            coord << " (" << str_reduce(sql->text(col_x))
                  <<  ',' << str_reduce(sql->text(col_y))
                  <<  ')';
            if (xerr || yerr) {
                coord << " +- (" << (xerr ? str_reduce(sql->text(col_xerr)) : "0")
                      << ',' << (yerr ? str_reduce(sql->text(col_yerr)) : "0")
                      << ')';
            }
        }

        // store last coordates group
        if (coord.str().size())
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
void SpLatex::tabular(
    size_t ln,
    size_t indent,
    const std::string& cmdline,
    const std::string& op_name,
    const std::string& separator,
    const std::string& endline,
    const std::string& gobble_regex)
{
    std::string query = cmdline;

    // find REFORMAT, parse format and remove clause from query
    Reformat reformat;
    reformat.parse_query(query);

    // execute query
    SqlQuery sql = g_db->query(query);

    sql->read_complete();

    // prepare reformatting
    reformat.prepare(sql);

    // calculate width of columns data
    std::vector<size_t> cwidth(sql->num_cols(), 0);

    for (unsigned int i = 0; i < sql->num_rows(); ++i)
    {
        for (unsigned int j = 0; j < sql->num_cols(); ++j)
        {
            cwidth[j] = std::max(
                cwidth[j],
                reformat.format(i,j, sql->text(i,j)).size()
                );
        }
    }

    // generate output
    std::vector<std::string> tlines;
    for (unsigned int i = 0; i < sql->num_rows(); ++i)
    {
        std::ostringstream out;
        for (unsigned j = 0; j < sql->num_cols(); ++j)
        {
            if (j != 0) out << separator;
            out << std::setw(cwidth[j])
                << reformat.format(i, j, sql->text(i,j));
        }
        out << endline;
        tlines.push_back(out.str());
    }

    // scan lines forward till next comment directive
    size_t eln = ln;
    while (eln < m_lines.size() && is_comment_line(eln) < 0)
        ++eln;

    const boost::regex
        re_endtabular("[[:blank:]]*% END " + op_name + " .*");

    if (eln < m_lines.size() &&
        boost::regex_match(m_lines[eln], re_endtabular))
    {
        // found END TABULAR
        size_t rln = ln;
        size_t entry = 0;

        const boost::regex re_tabular(gobble_regex);
        boost::smatch rm;

        // iterate over tabular lines, copy styles to replacement
        while (entry < tlines.size() && rln < eln &&
               boost::regex_match(m_lines[rln], rm, re_tabular))
        {
            tlines[entry++] += rm[1];
            ++rln;
        }

        tlines.push_back(shorten("% END " + op_name + " " + query));
        m_lines.replace(ln, eln+1, indent, tlines, op_name);
    }
    else
    {
        // could not find END TABULAR: insert whole table.
        tlines.push_back(shorten("% END " + op_name + " " + query));
        m_lines.replace(ln, ln, indent, tlines, op_name);
    }
}

//! Process % DEFMACRO commands
void SpLatex::defmacro(size_t ln, size_t indent, const std::string& cmdline)
{
    std::string query = cmdline;

    // find REFORMAT, parse format and remove clause from query
    Reformat reformat;
    reformat.parse_query(query);

    // execute query
    SqlQuery sql = g_db->query(query);

    sql->read_complete();

    // prepare reformatting
    reformat.prepare(sql);

    std::ostringstream oss;
    for (unsigned int row = 0; row < sql->num_rows(); ++row)
    {
        for (unsigned int col = 0; col < sql->num_cols(); ++col)
        {
            if (col != 0) oss << std::endl;
            oss << "\\def\\"
                << str_reduce(sql->col_name(col))
                << "{"
                << reformat.format(0, col, sql->text(0, col))
                << "}";
        }
    }

    std::string output = oss.str();

    // scan lines forward and gobble all lines containing \def commands
    static const boost::regex
        re_defmacro("[[:blank:]]*\\\\def\\\\[^{]+\\{[^}]+\\}.*");
    boost::smatch rm;

    size_t eln = ln;
    while (eln < m_lines.size() &&
           boost::regex_match(m_lines[eln], rm, re_defmacro))
    {
        ++eln;
    }

    m_lines.replace(ln, eln, indent, output, "DEFMACRO");
}

//! process line-based file in place
SpLatex::SpLatex(TextLines& lines)
    : m_lines(lines)
{
    bool active_range = gopt_ranges.size() ? false : true;

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

        if (first_word == "RANGE")
        {
            // extract second word
            std::string::size_type non_space_pos =
                cmd.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-_", space_pos);

            std::string::size_type space2_pos =
                cmd.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ-_", non_space_pos);

            std::string::size_type non_space2_pos =
                cmd.find_first_not_of(" ", space2_pos);

            std::string second_word = cmd.substr(
                non_space_pos, space2_pos - non_space_pos);

            std::string rest_word = cmd.substr(non_space2_pos);

            if (second_word == "BEGIN")
            {
                if (std::find(gopt_ranges.begin(), gopt_ranges.end(),
                              rest_word) != gopt_ranges.end())
                {
                    OUT(ln << " % " << cmd);
                    active_range = true;
                }
            }
            else if (second_word == "END")
            {
                if (std::find(gopt_ranges.begin(), gopt_ranges.end(),
                              rest_word) != gopt_ranges.end())
                {
                    OUT(ln << " % " << cmd);
                    active_range = false;
                }
            }
            else
            {
                OUT("? maybe unknown keywords " << first_word << " " << second_word);
            }
        }
        else if (!active_range)
        {
            // skip keywords in non-active ranges
        }
        else if (first_word == "SQL")
        {
            OUT(ln << " % " << cmd);
            sql(ln, indent, cmd.substr(space_pos+1));
        }
        else if (first_word == "IMPORT-DATA")
        {
            OUT(ln << " % " << cmd);
            importdata(ln, indent, cmd);
        }
        else if (first_word == "CONNECT")
        {
            OUT(ln << " % " << cmd);
            if (!connect(ln, indent, cmd.substr(space_pos+1)))
                OUT_THROW("Database connection lost.");
        }
        else if (first_word == "TEXTTABLE")
        {
            OUT(ln << " % " << cmd);
            texttable(ln, indent, cmd.substr(space_pos+1));
        }
        else if (first_word == "PLOT")
        {
            OUT(ln << " % " << cmd);
            plot(ln, indent, cmd.substr(space_pos+1));
        }
        else if (first_word == "MULTIPLOT")
        {
            OUT(ln << " % " << cmd);
            multiplot(ln, indent, cmd);
        }
        else if (first_word == "TABULAR")
        {
            OUT(ln << " % " << cmd);
            tabular(ln, indent, cmd.substr(space_pos+1),
                    "TABULAR", " & ", " \\\\", ".*?\\\\\\\\(.*)");
        }
        else if (first_word == "TABTABLE")
        {
            OUT(ln << " % " << cmd);
            tabular(ln, indent, cmd.substr(space_pos+1),
                    "TABTABLE", "\t", "", ".*\\t.*()");
        }
        else if (first_word == "DEFMACRO")
        {
            OUT(ln << " % " << cmd);
            defmacro(ln, indent, cmd.substr(space_pos+1));
        }
        else
        {
            if (first_word.size() >= 4 && first_word[0] != '-')
                OUT("? maybe unknown keyword " << first_word);
        }
    }
}

//! Process LaTeX file
void sp_latex(const std::string& /* filename */, TextLines& lines)
{
    SpLatex sp(lines);
}
