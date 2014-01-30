/******************************************************************************
 * src/sql.cpp
 *
 * Encapsulate SQL queries into a generic C++ class, and specialize for
 * different SQL implementations.
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

#include "sql.h"
#include "common.h"
#include "strtools.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

SqlQueryImpl::SqlQueryImpl(const std::string& query)
    : m_query(query)
{
}

SqlQueryImpl::~SqlQueryImpl()
{
}

//! Return query string.
const std::string& SqlQueryImpl::query() const
{
    return m_query;
}

//! Read column name map for the following col -> num mappings.
void SqlQueryImpl::read_colmap()
{
    m_colmap.clear();

    for (unsigned int col = 0; col < num_cols(); ++col)
        m_colmap[ col_name(col) ] = col;
}

//! Check if a column name exists.
bool SqlQueryImpl::exist_col(const std::string& name) const
{
    return (m_colmap.find(name) != m_colmap.end());
}

//! Returns column number of name or throws if it does not exist.
unsigned int SqlQueryImpl::find_col(const std::string& name) const
{
    colmap_type::const_iterator it = m_colmap.find(name);

    if (it == m_colmap.end())
    {
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Column " << name << " not found in result!");
    }

    return it->second;
}

//! Format result as a text table
std::string SqlQueryImpl::format_texttable()
{
    read_complete();

    // format SQL table: read data and determine column widths

    std::vector<size_t> width(num_cols(), 0);
    std::vector<bool> is_number(num_cols(), true);

    for (unsigned int col = 0; col < num_cols(); ++col)
    {
        width[col] = std::max(width[col], col_name(col).size() );
    }

    for (unsigned int row = 0; row < num_rows(); ++row)
    {
        for (unsigned int col = 0; col < num_cols(); ++col)
        {
            width[col] = std::max(width[col], strlen(text(row, col)) );

            if (is_number[col] && !str_is_double(text(row, col)))
                is_number[col] = false;
        }
    }

    // construct header/middle/footer breaks
    std::ostringstream obreak;
    obreak << "+-";
    for (unsigned int col = 0; col < num_cols(); ++col)
    {
        if (col != 0) obreak << "+-";
        obreak << std::string(width[col]+1, '-');
    }
    obreak << "+" << std::endl;

    // format output
    std::ostringstream os;

    os << obreak.str();

    os << "| ";
    for (unsigned int col = 0; col < num_cols(); ++col)
    {
        if (col != 0) os << "| ";
        os << std::setw(width[col]) << std::right
           << col_name(col) << ' ';
    }
    os << '|' << std::endl;
    os << obreak.str();

    for (unsigned int row = 0; row < num_rows(); ++row)
    {
        os << "| ";
        for (unsigned int col = 0; col < num_cols(); ++col)
        {
            if (col != 0) os << "| ";
            os << std::setw(width[col]);

            if (is_number[col])
                os << std::right;
            else
                os << std::left;

            os << text(row, col) << ' ';
        }
        os << '|' << std::endl;
    }
    os << obreak.str();

    return os.str();
}

////////////////////////////////////////////////////////////////////////////////

SqlDatabase::~SqlDatabase()
{
}
