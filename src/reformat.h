/******************************************************************************
 * src/reformat.h
 *
 * Reformatting class for LaTeX output
 *
 ******************************************************************************
 * Copyright (C) 2014 Timo Bingmann <tb@panthema.net>
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

#ifndef REFORMAT_HEADER
#define REFORMAT_HEADER

#include <limits>
#include <map>
#include <set>
#include <string>

#include "sql.h"

/*!
 * Reformatting styles for text or double value cells.
 *
 * cell-level formats:
 *  - escape (escape special LaTeX characters)
 *  - round=# (# = 'floor', 'ceil' or number of decimal digits)
 *  - precision=# (# = show decimal digits)
 *  - width=# (# = set width of output)
 *  - digits=# (# = show 3,4,5 decimal digits)
 *  - group, grouping (show thousand grouping symbol)
 *
 * column selector:
 *  - col/cols/column/columns 1-2,3-4,5,6=(<column-format>)
 *
 * column-level formats:
 *  - all cell-level formats
 *  - max/min/maximum/minimum=<style>
 *    with style:
 *    - bold -> \bf in LaTeX
 *    - emph -> \em in LaTeX
 */
class Reformat
{
    //! output debugging information while parsing
    static const bool debug = false;

protected:

    //! split next word into key, advance end as needed.
    static bool
    parse_keyword(std::string::const_iterator& curr,
                  const std::string::const_iterator& end,
                  std::string& key);

    //! read additional argument of key
    static std::string
    parse_keyargument(std::string::const_iterator& curr,
                      const std::string::const_iterator& end);

    //! read a " value", " (values)", "=value" or "=(values)" string from curr
    static std::string
    parse_keyvalue(std::string::const_iterator& curr,
                   const std::string::const_iterator& end);

    //! read a "=value" or "=(values)" string from curr
    static bool
    parse_keyvalue(std::string::const_iterator& curr,
                   const std::string::const_iterator& end,
                   std::string& value);

    //! parse a list of number (ranges) "3,4 - 7,10-11" into a number set
    static std::set<unsigned>
    parse_numbers(const std::string& text);

protected:
    //! Specifications for cell-level formats
    struct Cell
    {
        //! escape special LaTeX characters
        bool m_escape;

        //! round double numbers, options: floor, ceil, round, precision
        enum { RD_UNDEF, RD_FLOOR, RD_CEIL, RD_ROUND, RD_DIGITS } m_round;

        //! number of decimal digits to round to
        int m_round_digits;

        //! precision of reformatted numbers
        int m_reformat_precision;

        //! width of reformatted numbers
        int m_reformat_width;

        //! width of reformatted numbers
        int m_reformat_digits;

        //! thousands grouping separator
        std::string m_grouping;

        //! Initialize with sentinels
        Cell()
            : m_escape(false),
              m_round(RD_UNDEF),
              m_round_digits(-1),
              m_reformat_precision(-1),
              m_reformat_width(-1),
              m_reformat_digits(-1)
        {
        }

        //! Parse cell-level key=value format
        bool parse_keyformat(const std::string& key,
                             std::string::const_iterator& curr,
                             const std::string::const_iterator& end);

        //! Apply formats of other cell-level object
        void apply(const Cell& c);
    };

    //! Specifications for row- or column-level formats
    struct Line : public Cell
    {
        //! Enum to specify formatting of min/max in column
        enum minmax_format_type { MF_UNDEF, MF_NONE, MF_BOLD, MF_EMPH };

        //! Formatting of minimum or maximum
        minmax_format_type m_min_format, m_max_format;

        //! Minimum and maximum values in row/column.
        double m_min_value, m_max_value;

        //! Minimum and maximum values in row/column, as text for matching.
        std::string m_min_text, m_max_text;

        //! Initialize row/column min/max with sentinels
        Line()
            : m_min_format(MF_UNDEF),
              m_max_format(MF_UNDEF),
              m_min_value( std::numeric_limits<double>::max() ),
              m_max_value( std::numeric_limits<double>::min() )
        {
        }

        //! Test if we need to read the row/column data
        bool readdata() const;

        //! Check for valid min/max format
        static minmax_format_type parse_minmax(const std::string& key,
                                               const std::string& value);

        //! Parse row/column-level key=value format
        bool parse_keyformat(const std::string& key,
                             std::string::const_iterator& curr,
                             const std::string::const_iterator& end);

        //! Parse row/column-level formating arguments
        bool parse_format(const std::string& format);

        //! Apply formats of other row/column-level object
        void apply(const Line& c);
    };

    //! Typedef of row/column format container
    typedef std::map<unsigned, Line> linefmt_type;

    //! Set of row-specific formats
    linefmt_type m_rowfmt;

    //! Set of column-specific formats
    linefmt_type m_colfmt;

    //! Default row/column-level and cell-level formats
    Line m_fmt;

public:

    //! detect REFORMAT(...) clause, parse and remove it from query.
    void parse_query(std::string& query);

    //! Parse top-level formatting arguments
    bool parse_format(const std::string& format);

    //! Prepare formatting by anaylsing SQL answer (must be completely cached!)
    void prepare(const SqlQuery& sql);

    //! Reformat SQL data in cell (row,col) according to formats
    std::string format(int row, int col, const std::string& in_text) const;
};

#endif // REFORMAT_HEADER
