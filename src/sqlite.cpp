/******************************************************************************
 * src/sqlite.cpp
 *
 * Encapsulate SQLite queries into a C++ class, which is a specialization of
 * the generic SQL database interface.
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

#include "sqlite.h"
#include "common.h"
#include "strtools.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <vector>

//! Execute a SQL query without parameters, throws on errors.
SQLiteQuery::SQLiteQuery(class SQLiteDatabase& db, const std::string& query)
    : SqlQueryImpl(query),
      m_db(db)
{
    const char* zTail = 0;

    int rc = sqlite3_prepare_v2(m_db.m_db, query.c_str(), query.size()+1,
                                &m_stmt, &zTail);
    if (rc != SQLITE_OK)
    {
        OUT_THROW("SQL query parse " << query << "\n" <<
                  "Failed at " << zTail << " : " << m_db.errmsg());
    }

    rc = sqlite3_step(m_stmt);

    if (rc == SQLITE_ROW)
    {
        m_row = -1;
    }
    else if (rc == SQLITE_DONE)
    {
        m_row = -2;
    }
    else
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed : " << m_db.errmsg());
    }
}

//! Execute a SQL query with parameters, throws on errors.
SQLiteQuery::SQLiteQuery(class SQLiteDatabase& db, const std::string& query,
                         const std::vector<std::string>& params)
    : SqlQueryImpl(query),
      m_db(db)
{
    const char* zTail = 0;

    int rc = sqlite3_prepare_v2(m_db.m_db, query.c_str(), query.size()+1,
                                &m_stmt, &zTail);
    if (rc != SQLITE_OK)
    {
        OUT_THROW("SQL query parse " << query << "\n" <<
                  "Failed at " << zTail << " : " << m_db.errmsg());
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        sqlite3_bind_text(m_stmt, i+1,
                          params[i].data(), params[i].size(), NULL);
    }

    rc = sqlite3_step(m_stmt);

    if (rc == SQLITE_ROW)
    {
        m_row = -1;
    }
    else if (rc == SQLITE_DONE)
    {
        m_row = -2;
    }
    else
    {
        OUT_THROW("SQL query " << query << "\n" <<
                  "Failed : " << m_db.errmsg());
    }
}

//! Free result
SQLiteQuery::~SQLiteQuery()
{
    sqlite3_finalize(m_stmt);
}

//! Return number of rows in result, throws if no tuples.
unsigned int SQLiteQuery::num_rows() const
{
    if (SqlDataCache::is_complete())
        return SqlDataCache::num_rows();

    assert(!"Row number not available without cached result.");
    return -1;
}

//! Return number of columns in result, throws if no tuples.
unsigned int SQLiteQuery::num_cols() const
{
    return sqlite3_column_count(m_stmt);
}

//! Return column name of col
std::string SQLiteQuery::col_name(unsigned int col) const
{
    return sqlite3_column_name(m_stmt, col);
}

//! Return the current row number
unsigned int SQLiteQuery::current_row() const
{
    return m_row;
}

//! Advance current result row to next (or first if uninitialized)
bool SQLiteQuery::step()
{
    if (m_row == -1) {
        m_row++;
        return true;
    }
    else if (m_row == -2) {
        return false;
    }

    int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
    {
        ++m_row;
        return true;
    }
    else if (rc == SQLITE_DONE)
    {
        ++m_row;
        return false;
    }
    else
    {
        OUT_THROW("SQL query " << query() << "\n" <<
                  "Step failed : " << m_db.errmsg());
    }
}

//! Returns true if cell (row,col) is NULL.
bool SQLiteQuery::isNULL(unsigned int col) const
{
    assert(col < num_cols());
    return sqlite3_column_type(m_stmt, col) == SQLITE_NULL;
}

//! Return text representation of column col of current row.
const char* SQLiteQuery::text(unsigned int col) const
{
    assert(col < num_cols());
    return (const char*)sqlite3_column_text(m_stmt, col);
}

//! read complete result into memory
void SQLiteQuery::read_complete()
{
    return SqlDataCache::read_complete(*this);
}

//! Returns true if cell (row,col) is NULL.
bool SQLiteQuery::isNULL(unsigned int row, unsigned int col) const
{
    return SqlDataCache::isNULL(row, col);
}

//! Return text representation of cell (row,col).
const char* SQLiteQuery::text(unsigned int row, unsigned int col) const
{
    return SqlDataCache::text(row, col);
}

////////////////////////////////////////////////////////////////////////////////

//! try to connect to the database with default parameters
bool SQLiteDatabase::initialize()
{
    // make connection to in-memory database
    int rc = sqlite3_open_v2(":memory:", &m_db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK)
    {
        OUT("Connection to SQLite3 database failed: " << sqlite3_errstr(rc));
        return false;
    }

    return true;
}

//! destructor to free connection
SQLiteDatabase::~SQLiteDatabase()
{
    sqlite3_close(m_db);
}

//! return type of SQL database
SQLiteDatabase::db_type SQLiteDatabase::type() const
{
    return DB_SQLITE;
}

//! return string for the i-th placeholder, where i starts at 0.
std::string SQLiteDatabase::placeholder(unsigned int i) const
{
    return "$" + to_str(i+1);
}

//! execute SQL query without result
bool SQLiteDatabase::execute(const std::string& query)
{
    char* zTail = 0;

    int rc = sqlite3_exec(m_db, query.c_str(), NULL, NULL, &zTail);

    if (rc != SQLITE_OK)
    {
        OUT_THROW("SQL query \"" << query << "\"\n" <<
                  "Failed at " << zTail << " : " << errmsg());
    }

    return true;
}

//! construct query object for given string
SqlQuery SQLiteDatabase::query(const std::string& query)
{
    return SqlQuery( new SQLiteQuery(*this, query) );
}

//! construct query object for given string with placeholder parameters
SqlQuery SQLiteDatabase::query(const std::string& query,
                              const std::vector<std::string>& params)
{
    return SqlQuery( new SQLiteQuery(*this, query, params) );
}

//! test if a table exists in the database
bool SQLiteDatabase::exist_table(const std::string& table)
{
    std::vector<std::string> params;
    params.push_back(table);

    SQLiteQuery sql(*this,
                    "SELECT COUNT(*) FROM sqlite_master "
                    "WHERE type='table' AND name = $1",
                    params);

    assert(sql.num_cols() == 1);
    if (!sql.step()) {
        OUT_THROW("exist_table() failed.");
    }

    return strcmp(sql.text(0), "0") != 0;
}

//! return last error message string
const char* SQLiteDatabase::errmsg() const
{
    return sqlite3_errmsg(m_db);
}

////////////////////////////////////////////////////////////////////////////////
