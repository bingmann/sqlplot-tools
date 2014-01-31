/******************************************************************************
 * src/pgsql.cpp
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
#include "strtools.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <vector>

//! Execute a SQL query without placeholders, throws on errors.
PgSqlQuery::PgSqlQuery(class PgSqlDatabase& db, const std::string& query)
    : SqlQueryImpl(query),
      m_db(db)
{
    m_res = PQexec(m_db.m_pg, query.c_str());

    ExecStatusType r = PQresultStatus(m_res);

    if (r == PGRES_BAD_RESPONSE ||
        r == PGRES_FATAL_ERROR)
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed with " << PQresStatus(r) <<
                  " : " << m_db.errmsg());
    }

    m_row = -1;
}

//! Execute a SQL query with placeholders, throws on errors.
PgSqlQuery::PgSqlQuery(class PgSqlDatabase& db, const std::string& query,
                       const std::vector<std::string>& params)
    : SqlQueryImpl(query),
      m_db(db)
{
    // construct vector of const char* for interface
    std::vector<const char*> paramsC(params.size());

    for (size_t i = 0; i < params.size(); ++i)
        paramsC[i] = params[i].c_str();

    // execute query with string variables

    m_res = PQexecParams(m_db.m_pg, query.c_str(),
                         params.size(), NULL, paramsC.data(), NULL, NULL, 0);

    ExecStatusType r = PQresultStatus(m_res);

    if (r == PGRES_BAD_RESPONSE ||
        r == PGRES_FATAL_ERROR)
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed with " << PQresStatus(r) <<
                  " : " << m_db.errmsg());
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
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Did not return tuples : " << m_db.errmsg());
    }

    int num = PQntuples(m_res);

    if (num < 0)
    {
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Did not return tuples : " << m_db.errmsg());
    }

    return num;
}

//! Return column name of col
std::string PgSqlQuery::col_name(unsigned int col) const
{
    return PQfname(m_res, col);
}

//! Return number of columns in result, throws if no tuples.
unsigned int PgSqlQuery::num_cols() const
{
    if (PQresultStatus(m_res) != PGRES_TUPLES_OK)
    {
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Did not return tuples : " << m_db.errmsg());
    }

    int num = PQnfields(m_res);

    if (num < 0)
    {
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Did not return tuples : " << m_db.errmsg());
    }

    return num;
}

//! Return the current row number
unsigned int PgSqlQuery::current_row() const
{
    return m_row;
}

//! Advance current result row to next (or first if uninitialized)
bool PgSqlQuery::step()
{
    ++m_row;
    return (m_row < num_rows());
}

//! Returns true if cell (row,col) is NULL.
bool PgSqlQuery::isNULL(unsigned int col) const
{
    assert(m_row < num_rows());
    assert(col < num_cols());
    return PQgetisnull(m_res, m_row, col);
}

//! Return text representation of column col of current row.
std::string PgSqlQuery::text(unsigned int col) const
{
    assert(m_row < num_rows());
    assert(col < num_cols());
    size_t length = PQgetlength(m_res, m_row, col);
    return std::string(PQgetvalue(m_res, m_row, col), length);
}

//! read complete result into memory
void PgSqlQuery::read_complete()
{
    // noop on PostgreSQL
    return;
}

//! Returns true if cell (row,col) is NULL.
bool PgSqlQuery::isNULL(unsigned int row, unsigned int col) const
{
    assert(row < num_rows());
    assert(col < num_cols());
    return PQgetisnull(m_res, row, col);
}

//! Return text representation of cell (row,col).
std::string PgSqlQuery::text(unsigned int row, unsigned int col) const
{
    assert(row < num_rows());
    assert(col < num_cols());
    size_t length = PQgetlength(m_res, row, col);
    return std::string(PQgetvalue(m_res, row, col), length);
}

////////////////////////////////////////////////////////////////////////////////

//! try to connect to the database with default parameters
bool PgSqlDatabase::initialize(const std::string& params)
{
    OUT("Connecting to PostgreSQL database \"" << params << "\".");

    // make connection to the database
    m_pg = PQconnectdb(params.c_str());

    // check to see that the backend connection was successfully made
    if (PQstatus(m_pg) != CONNECTION_OK)
    {
        OUT("Connection to PostgreSQL failed: " << errmsg());
        return false;
    }

    return true;
}

//! destructor to free connection
PgSqlDatabase::~PgSqlDatabase()
{
    PQfinish(m_pg);
}

//! return type of SQL database
PgSqlDatabase::db_type PgSqlDatabase::type() const
{
    return DB_PGSQL;
}

//! return string for the i-th placeholder, where i starts at 0.
std::string PgSqlDatabase::placeholder(unsigned int i) const
{
    return '$' + to_str(i+1);
}

//! return quoted table or field identifier
std::string PgSqlDatabase::quote_field(const std::string& field) const
{
    return '"' + field + '"';
}

//! execute SQL query without result
bool PgSqlDatabase::execute(const std::string& query)
{
    PGresult* res = PQexec(m_pg, query.c_str());

    ExecStatusType r = PQresultStatus(res);

    if (r == PGRES_TUPLES_OK)
    {
        OUT("SQL query " << query << "\n" <<
            "Return TUPLES!!!");
    }
    else if (r != PGRES_COMMAND_OK)
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed with " << PQresStatus(r) <<
                  " : " << errmsg());
    }

    PQclear(res);

    return true;
}

//! construct query object for given string
SqlQuery PgSqlDatabase::query(const std::string& query)
{
    return SqlQuery( new PgSqlQuery(*this, query) );
}

//! construct query object for given string with placeholder parameters
SqlQuery PgSqlDatabase::query(const std::string& query,
                              const std::vector<std::string>& params)
{
    return SqlQuery( new PgSqlQuery(*this, query, params) );
}

//! test if a table exists in the database
bool PgSqlDatabase::exist_table(const std::string& table)
{
    std::vector<std::string> params;
    params.push_back(table);

    PgSqlQuery sql(*this,
                   "SELECT COUNT(*) FROM pg_tables WHERE tablename = $1",
                   params);

    assert(sql.num_rows() == 1 && sql.num_cols() == 1);
    sql.step();

    return (sql.text(0) != "0");
}

//! return last error message string
const char* PgSqlDatabase::errmsg() const
{
    return PQerrorMessage(m_pg);
}

////////////////////////////////////////////////////////////////////////////////
