/******************************************************************************
 * src/sql.cpp
 *
 * Encapsulate PostgreSQL queries into a C++ class, which is a specialization
 * of the generic SQL database interface.
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

#include "pgsql.h"
#include "common.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <vector>

//! Execute a SQL query without parameters, throws on errors.
PgSqlQuery::PgSqlQuery(const std::string& query)
    : m_query(query)
{
    m_res = PQexec(g_pg, query.c_str());

    ExecStatusType r = PQresultStatus(m_res);

    if (r == PGRES_BAD_RESPONSE ||
        r == PGRES_FATAL_ERROR)
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed with " << PQresStatus(r) <<
                  " : " << PQerrorMessage(g_pg));
    }

    m_row = -1;
}

//! Free result
PgSqlQuery::~PgSqlQuery()
{
    PQclear(m_res);
}

//! Return number of rows in result, throws if no tuples.
unsigned int PgSqlQuery::num_rows() const
{
    if (PQresultStatus(m_res) != PGRES_TUPLES_OK)
    {
        OUT_THROW("SQL query " << m_query << "\n" <<
                  "Did not return tuples : " << PQerrorMessage(g_pg));
    }

    int num = PQntuples(m_res);

    if (num < 0)
    {
        OUT_THROW("SQL query " << m_query << "\n" <<
                  "Did not return tuples : " << PQerrorMessage(g_pg));
    }

    return num;
}

//! Return column name of col
const char* PgSqlQuery::col_name(unsigned int col) const
{
    return PQfname(m_res, col);
}

//! Return number of columns in result, throws if no tuples.
unsigned int PgSqlQuery::num_cols() const
{
    if (PQresultStatus(m_res) != PGRES_TUPLES_OK)
    {
        OUT_THROW("SQL query " << m_query << "\n" <<
                  "Did not return tuples : " << PQerrorMessage(g_pg));
    }

    int num = PQnfields(m_res);

    if (num < 0)
    {
        OUT_THROW("SQL query " << m_query << "\n" <<
                  "Did not return tuples : " << PQerrorMessage(g_pg));
    }

    return num;
}

//! Read column name map for the following col -> num mappings.
PgSqlQuery& PgSqlQuery::read_colmap()
{
    m_colmap.clear();

    for (unsigned int col = 0; col < num_cols(); ++col)
        m_colmap[ PQfname(m_res, col) ] = col;

    return *this;
}

//! Check if a column name exists.
bool PgSqlQuery::exist_col(const std::string& name) const
{
    return (m_colmap.find(name) != m_colmap.end());
}

//! Returns column number of name or throws if it does not exist.
unsigned int PgSqlQuery::find_col(const std::string& name) const
{
    colmap_type::const_iterator it = m_colmap.find(name);

    if (it == m_colmap.end())
    {
        OUT_THROW("SQL query " << m_query << "\n" <<
                  "Column " << name << " not found in result!");
    }

    return it->second;
}

//! Advance current result row to next (or first if uninitialized)
bool PgSqlQuery::step()
{
    ++m_row;
    return (m_row < num_rows());
}

//! Return the current row number
unsigned int PgSqlQuery::curr_row() const
{
    return m_row;
}

//! Return text representation of column col of current row.
const char* PgSqlQuery::text(unsigned int col) const
{
    assert(m_row < num_rows());
    assert(col < num_cols());
    return PQgetvalue(m_res, m_row, col);
}

//! read complete result into memory
PgSqlQuery& PgSqlQuery::read_complete()
{
    // noop on PostgreSQL
    return *this;
}

//! Return text representation of cell (row,col).
const char* PgSqlQuery::text(unsigned int row, unsigned int col) const
{
    return PQgetvalue(m_res, row, col);
}

//! format result as a text table
std::string PgSqlQuery::format_texttable()
{
    read_complete();

    // format SQL table: read data and determine column widths

    std::vector<size_t> width(num_cols(), 0);

    for (unsigned int col = 0; col < num_cols(); ++col)
    {
        width[col] = std::max(width[col], strlen( col_name(col) ));
    }

    for (unsigned int row = 0; row < num_rows(); ++row)
    {
        for (unsigned int col = 0; col < num_cols(); ++col)
        {
            width[col] = std::max(width[col], strlen(text(row, col)) );
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

            //os << '-' << PQftype(m_res, col) << '-';
            if (PQftype(m_res, col) == 23 || PQftype(m_res, col) == 20)
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
