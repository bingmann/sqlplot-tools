/*******************************************************************************
 * src/reformat.cpp
 *
 * Reformatting class for LaTeX output
 *
 *******************************************************************************
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
 ******************************************************************************/

#include "reformat.h"
#include "strtools.h"
#include "common.h"

#include <cmath>
#include <locale>
#include <iomanip>

//! read next word into key, advance end as needed.
bool Reformat::parse_keyword(std::string::const_iterator& curr,
                             const std::string::const_iterator& end,
                             std::string& key)
{
    if (curr == end) return false;

    // skip over spaces
    while (curr != end && isblank(*curr)) ++curr;

    if (curr == end) return false;

    // read key
    std::string::const_iterator ckey = curr;
    while (curr != end && isalnum(*curr)) ++curr;

    key = std::string(ckey, curr);

    return true;
}

//! read additional argument of key
std::string Reformat::parse_keyargument(std::string::const_iterator& curr,
                                        const std::string::const_iterator& end)
{
    if (curr == end) return std::string();

    // skip over spaces
    while (curr != end && isblank(*curr)) ++curr;

    if (curr == end) return std::string();

    // read key
    std::string::const_iterator ckey = curr;
    while (curr != end && *curr != '=' && *curr != '(') ++curr;

    return std::string(ckey, curr);
}

//! read a " value", " (values)", "=value" or "=(values)" string from curr
std::string Reformat::parse_keyvalue(std::string::const_iterator& curr,
                                      const std::string::const_iterator& end)
{
    if (curr == end) return std::string();

    // skip over spaces
    while (curr != end && isblank(*curr)) ++curr;

    if (curr == end) return std::string();

    // read key separator = or plain space

    if (*curr == '=')
    {
        ++curr;

        // skip over spaces after =
        while (curr != end && isblank(*curr)) ++curr;
    }

    if (*curr == '(')
    {
        // collect text until balanced closing parentheses
        int depth = 0;

        std::string::const_iterator cvalue = ++curr;
        while (curr != end)
        {
            if (*curr == '(') ++depth;
            else if (*curr == ')' && depth == 0) break;
            else if (*curr == ')') --depth;
            ++curr;
        }

        if (curr == end)
            OUT_THROW("Unbalanced parentheses in REFORMAT() clause.");

        assert(*curr == ')');
        return std::string(cvalue, curr++);
    }
    else
    {
        // collect text until next space
        std::string::const_iterator cvalue = curr;

        while (curr != end && !isblank(*curr)) ++curr;

        return std::string(cvalue, curr);
    }
}

//! read a "=value" or "=(values)" string from curr
bool Reformat::parse_keyvalue(std::string::const_iterator& curr,
                              const std::string::const_iterator& end,
                              std::string& value)
{
    if (curr == end) return false;

    // skip over spaces
    while (curr != end && isblank(*curr)) ++curr;

    if (curr == end) return false;

    // read key separator = or plain space

    if (*curr != '=') return false;
    ++curr;

    // skip over spaces after =
    while (curr != end && isblank(*curr)) ++curr;

    if (*curr == '(')
    {
        // collect text until balanced closing parentheses
        int depth = 0;

        std::string::const_iterator cvalue = ++curr;
        while (curr != end)
        {
            if (*curr == '(') ++depth;
            else if (*curr == ')' && depth == 0) break;
            else if (*curr == ')') --depth;
            ++curr;
        }

        if (curr == end)
            OUT_THROW("Unbalanced parentheses in REFORMAT() clause.");

        assert(*curr == ')');
        value = std::string(cvalue, curr++);
    }
    else
    {
        // collect text until next space
        std::string::const_iterator cvalue = curr;

        while (curr != end && !isblank(*curr)) ++curr;

        value = std::string(cvalue, curr);
    }

    return true;
}

//! parse a list of number (ranges) "3,4-7,10-11" into a number set
std::set<unsigned>
Reformat::parse_numbers(const std::string& text)
{
    typedef std::string::const_iterator iter_type;

    std::set<unsigned> out;

    iter_type c = text.begin();

    while (c != text.end())
    {
        // skip over spaces
        while (c != text.end() && isblank(*c)) ++c;

        // collect first number
        iter_type c1 = c;

        while (c != text.end() && isdigit(*c)) ++c;

        unsigned i1;
        if (!from_str(std::string(c1, c), i1))
            OUT_THROW("Error parsing number in range " << text);

        // skip over spaces
        while (c != text.end() && isblank(*c)) ++c;

        if (c == text.end()) {
            // plain number, and at the end
            out.insert(i1);
        }
        else if (*c == ',') {
            // plain number, in list
            out.insert(i1);
            ++c;
        }
        else if (*c == '-') {
            // collect second number
            ++c;

            // skip over spaces
            while (c != text.end() && isblank(*c)) ++c;

            // collect first number
            iter_type c2 = c;

            while (c != text.end() && isdigit(*c)) ++c;

            unsigned i2;
            if (!from_str(std::string(c2, c), i2))
                OUT_THROW("Error parsing number in range " << text);

            // skip over spaces
            while (c != text.end() && isblank(*c)) ++c;

            if (c == text.end() || *c == ',') {
                // plain number, insert range

                if (i1 > i2)
                    OUT_THROW("Invalid negative range in numbers " << text);

                for (unsigned i = i1; i <= i2; ++i)
                    out.insert(i);

                if (c != text.end())
                    ++c;
            }
            else {
                OUT_THROW("Invalid character in number of range " << text);
            }
        }
        else {
            OUT_THROW("Invalid character in number of range " << text);
        }
    }

    return out;
}

//! Parse cell-level key=value format
bool Reformat::Cell::parse_keyformat(const std::string& key,
                                     std::string::const_iterator& curr,
                                     const std::string::const_iterator& end)
{
    if (key == "floor")
    {
        m_round = RD_FLOOR;
        return true;
    }
    else if (key == "ceil")
    {
        m_round = RD_CEIL;
        return true;
    }
    else if (key == "round")
    {
        std::string value = parse_keyvalue(curr, end);

        if (value == "floor")
            m_round = RD_FLOOR;
        else if (value == "ceil")
            m_round = RD_CEIL;
        else if (from_str(value, m_round_digits))
            m_round = RD_ROUND;
        else
            OUT_THROW("Invalid cell-level round format: "
                      << key << '=' << value);

        return true;
    }
    else if (key == "precision")
    {
        std::string value = parse_keyvalue(curr, end);

        if (!from_str(value, m_reformat_precision))
            OUT_THROW("Invalid cell-level precision format: "
                      << key << '=' << value);

        return true;
    }
    else if (key == "width")
    {
        std::string value = parse_keyvalue(curr, end);

        if (!from_str(value, m_reformat_width))
            OUT_THROW("Invalid cell-level width format: "
                      << key << '=' << value);

        return true;
    }
    else if (key == "digits")
    {
        std::string value = parse_keyvalue(curr, end);

        if (!from_str(value, m_reformat_digits))
            OUT_THROW("Invalid cell-level digits format: "
                      << key << '=' << value);

        return true;
    }
    else if (key == "group")
    {
        std::string value;

        if (!parse_keyvalue(curr, end, value))
            value = ",";

        m_grouping = value;

        return true;
    }
    else
    {
        OUT_THROW("Invalid cell-level key: " << key);
    }

    return false;
}

//! Apply formats of other cell-level object
void Reformat::Cell::apply(const Cell& c)
{
    if (c.m_round != RD_UNDEF)
    {
        m_round = c.m_round;
        m_round_digits = c.m_round_digits;
    }

    if (c.m_reformat_precision >= 0)
        m_reformat_precision = c.m_reformat_precision;

    if (c.m_reformat_width >= 0)
        m_reformat_width = c.m_reformat_width;

    if (c.m_reformat_digits >= 0)
        m_reformat_digits = c.m_reformat_digits;

    if (c.m_grouping.size())
        m_grouping = c.m_grouping;
}

//! Test if we need to read the row/column data
bool Reformat::Line::readdata() const
{
    if (m_min_format == MF_BOLD || m_min_format == MF_EMPH)
        return true;

    if (m_max_format == MF_BOLD || m_max_format == MF_EMPH)
        return true;

    return false;
}

//! Check for valid min/max format
Reformat::Line::minmax_format_type
Reformat::Line::parse_minmax(const std::string& key, const std::string& value)
{
    if (value == "") return MF_NONE;
    if (value == "bold" || value == "bf") return MF_BOLD;
    if (value == "emph" || value == "em") return MF_EMPH;

    OUT_THROW("Invalid formatting for row/column key " << key << ": " << value);
}

//! Parse row/column-level key=value format
bool Reformat::Line::parse_keyformat(const std::string& key,
                                     std::string::const_iterator& curr,
                                     const std::string::const_iterator& end)
{
    if (key == "min" || key == "minimum")
    {
        std::string value = parse_keyvalue(curr, end);

        m_min_format = parse_minmax(key, value);
    }
    else if (key == "max" || key == "maximum")
    {
        std::string value = parse_keyvalue(curr, end);

        m_max_format = parse_minmax(key, value);
    }
    else if (Cell::parse_keyformat(key, curr, end))
    {
        // key=value format processed as cell-level format
    }
    else
    {
        OUT_THROW("Invalid row/column-level format key: " << key);
    }

    return true;
}

//! Parse row/column-level formating arguments
bool Reformat::Line::parse_format(const std::string& format)
{
    std::string::const_iterator curr = format.begin();
    std::string key;

    while (parse_keyword(curr, format.end(), key))
    {
        DBG("row/column-level format key: " << key << "-");

        parse_keyformat(key, curr, format.end());
    }

    return true;
}

//! Apply formats of other row/column-level object
void Reformat::Line::apply(const Line& c)
{
    if (c.m_min_format != MF_UNDEF)
    {
        m_min_format = c.m_min_format;
        m_min_value = c.m_min_value;
        m_min_text = c.m_min_text;
    }

    if (c.m_max_format != MF_UNDEF)
    {
        m_max_format = c.m_max_format;
        m_max_value = c.m_max_value;
        m_max_text = c.m_max_text;
    }

    Cell::apply(c);
}

//! detect REFORMAT(...) clause, parse and remove it from query.
void Reformat::parse_query(std::string& query)
{
    if (!is_prefix(query, "REFORMAT")) return;

    // find opening parentheses
    std::string::const_iterator f1 = query.begin() + 8;
    while (f1 != query.end() && isblank(*f1)) ++f1;

    if (*f1++ != '(')
        OUT_THROW("Invalid REFORMAT clause: no parentheses");

    // collect text until balanced closing parentheses
    int depth = 0;

    std::string::const_iterator f2 = f1;
    while (f2 != query.end())
    {
        if (*f2 == '(') ++depth;
        else if (*f2 == ')' && depth == 0) break;
        else if (*f2 == ')') --depth;
        ++f2;
    }

    parse_format( trim(std::string(f1,f2)) );

    // trim query

    ++f2; // end of format
    query = trim( query.substr(f2 - query.begin()) );
}

//! Parse top-level formatting arguments
bool Reformat::parse_format(const std::string& format)
{
    DBG("top-level format: " << format);

    std::string::const_iterator curr = format.begin();
    std::string key;

    while (parse_keyword(curr, format.end(), key))
    {
        DBG("top-level format key: " << key << "-");

        if (key == "col" || key == "cols" ||
            key == "column" || key == "columns")
        {
            std::string arg = parse_keyargument(curr, format.end());

            DBG("top-level format key: " << key << " arg: " << arg << "-");

            std::set<unsigned> cols = parse_numbers(arg);

            Line colfmt;
            colfmt.parse_format(parse_keyvalue(curr, format.end()));

            for (std::set<unsigned>::iterator c = cols.begin();
                 c != cols.end(); ++c)
            {
                m_colfmt[*c].apply(colfmt);
            }
        }
        else if (key == "row" || key == "rows")
        {
            std::string arg = parse_keyargument(curr, format.end());

            DBG("top-level format key: " << key << " arg: " << arg << "-");

            std::set<unsigned> rows = parse_numbers(arg);

            Line rowfmt;
            rowfmt.parse_format(parse_keyvalue(curr, format.end()));

            for (std::set<unsigned>::iterator r = rows.begin();
                 r != rows.end(); ++r)
            {
                m_rowfmt[*r].apply(rowfmt);
            }
        }
        else if (m_fmt.parse_keyformat(key, curr, format.end()))
        {
            // parsed into default format
        }
        else
        {
            OUT_THROW("Invalid top-level format key: " << key);
        }
    }

    return true;
}

//! Prepare formatting by anaylsing SQL answer (must be completely cached!)
void Reformat::prepare(const SqlQuery& sql)
{
    for (unsigned i = 0; i < sql->num_rows(); ++i)
    {
        for (unsigned j = 0; j < sql->num_cols(); ++j)
        {
            if (!m_rowfmt[i].readdata() && !m_colfmt[j].readdata() &&
                !m_fmt.readdata())
                continue;

            std::string text = sql->text(i,j);
            if (text.size() == 0) continue;

            double v;
            if (from_str(text, v))
            {
                if (v < m_rowfmt[i].m_min_value)
                {
                    m_rowfmt[i].m_min_value = v;
                    m_rowfmt[i].m_min_text = text;
                }

                if (v > m_rowfmt[i].m_max_value)
                {
                    m_rowfmt[i].m_max_value = v;
                    m_rowfmt[i].m_max_text = text;
                }

                if (v < m_colfmt[j].m_min_value)
                {
                    m_colfmt[j].m_min_value = v;
                    m_colfmt[j].m_min_text = text;
                }

                if (v > m_colfmt[j].m_max_value)
                {
                    m_colfmt[j].m_max_value = v;
                    m_colfmt[j].m_max_text = text;
                }
            }
        }
    }
}

//! Traits class for formatting with thousands grouping
class CommaGrouping : public std::numpunct<char>
{
protected:
    //! provides the character to use as decimal point
    virtual char do_decimal_point() const { return '.'; }

    //! provides the character to use as thousands separator
    virtual char do_thousands_sep() const { return ','; }

    //! provides the numbers of digits between each pair of thousands
    //! separators
    virtual std::string do_grouping() const { return "\03"; }
};

//! Reformat SQL data in cell (row,col) according to formats
std::string Reformat::format(int row, int col, const std::string& in_text) const
{
    std::string text = in_text;

    if (text.size() == 0) return text;

    double v;
    if (from_str(text, v))
    {
        Line fmt = m_fmt;

        {
            linefmt_type::const_iterator rowfmt = m_rowfmt.find(row);
            if (rowfmt != m_rowfmt.end())
                fmt.apply(rowfmt->second);

            linefmt_type::const_iterator colfmt = m_colfmt.find(col);
            if (colfmt != m_colfmt.end())
                fmt.apply(colfmt->second);
        }

        // *** Round Double Number ***

        if (fmt.m_round == Cell::RD_FLOOR)
        {
            v = floor(v);

            if (fmt.m_reformat_precision < 0)
                fmt.m_reformat_precision = 0;
        }
        else if (fmt.m_round == Cell::RD_CEIL)
        {
            v = ceil(v);

            if (fmt.m_reformat_precision < 0)
                fmt.m_reformat_precision = 0;
        }
        else if (fmt.m_round == Cell::RD_ROUND)
        {
            double p = pow(10, fmt.m_round_digits);
            v = round(v * p) / p;

            if (fmt.m_reformat_precision < 0)
                fmt.m_reformat_precision = std::max(0, fmt.m_round_digits);
        }

        // *** Reformat Double Number ***

        if (fmt.m_reformat_precision >= 0 ||
            fmt.m_reformat_width >= 0 ||
            fmt.m_reformat_digits >= 0 ||
            fmt.m_grouping.size())
        {
            std::ostringstream oss;

            std::locale comma_locale(std::locale(), new CommaGrouping());
            oss.imbue(comma_locale);

            if (fmt.m_reformat_digits >= 0)
            {
                if (fmt.m_reformat_digits == 2) {
                    if (v < 1) {
                        // not 2: need leading 0.
                        oss << std::fixed << std::setprecision(2);
                    }
                    else if (v < 10) {
                        oss << std::fixed << std::setprecision(1);
                    }
                    else {
                        oss << std::fixed << std::setprecision(0);
                    }
                }
                else if (fmt.m_reformat_digits == 3) {
                    if (v < 1) {
                        // not 3: need leading 0.
                        oss << std::fixed << std::setprecision(3);
                    }
                    else if (v < 10) {
                        oss << std::fixed << std::setprecision(2);
                    }
                    else if (v < 100) {
                        oss << std::fixed << std::setprecision(1);
                    }
                    else {
                        oss << std::fixed << std::setprecision(0);
                    }
                }
                else if (fmt.m_reformat_digits == 4) {
                    if (v < 1) {
                        // not 4: need leading 0.
                        oss << std::fixed << std::setprecision(4);
                    }
                    else if (v < 10) {
                        oss << std::fixed << std::setprecision(3);
                    }
                    else if (v < 100) {
                        oss << std::fixed << std::setprecision(2);
                    }
                    else if (v < 1000) {
                        oss << std::fixed << std::setprecision(1);
                    }
                    else {
                        oss << std::fixed << std::setprecision(0);
                    }
                }
                else {
                    OUT_THROW("Error, currently only digits={2,3,4} is implemented.");
                }
            }
            else
            {
                oss << std::fixed;

                if (fmt.m_reformat_precision >= 0)
                    oss.precision(fmt.m_reformat_precision);

                if (fmt.m_reformat_width >= 0)
                    oss.width(fmt.m_reformat_width);
            }

            oss << v;

            text = oss.str();
        }

        // *** replace , with group formatting ***

        text = replace_all(text, ",", fmt.m_grouping);

        // *** check for row/column minimum or maximum formatting ***

        DBG("fmt: " << in_text << " - " << fmt.m_min_text);

        if (in_text == fmt.m_min_text)
        {
            if (fmt.m_min_format == Line::MF_BOLD)
                text = "\\textbf{" + text + "}";
            else if (fmt.m_min_format == Line::MF_EMPH)
                text = "\\emph{" + text + "}";
        }
        else if (in_text == fmt.m_max_text)
        {
            if (fmt.m_max_format == Line::MF_BOLD)
                text = "\\textbf{" + text + "}";
            else if (fmt.m_max_format == Line::MF_EMPH)
                text = "\\emph{" + text + "}";
        }
    }

    return text;
}
