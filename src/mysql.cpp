/******************************************************************************
 * src/mysql.cpp
 *
 * Encapsulate MySQL queries into a C++ class, which is a specialization of the
 * generic SQL database interface.
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

#include "mysql.h"
#include "common.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <vector>

//! Opaque structure for MYSQL_BIND
struct MySqlBind : public MYSQL_BIND
{
};

//! Class for result value
struct MySqlColumn
{
    //! NULL value indicator
    my_bool is_null;

    //! output string data, currently extra long strings are truncated.
    char strdata[128];

    //! output string data length
    unsigned long length;

    //! initialize internal bind pointers
    void initialize_as_string(MySqlBind& bind)
    {
        // buffer
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = strdata;
        bind.buffer_length = sizeof(strdata);
        // null
        bind.is_null = &is_null;
        // length
        bind.length = &length;
    }
};

//! Execute a SQL query without parameters, throws on errors.
MySqlQuery::MySqlQuery(class MySqlDatabase& db, const std::string& query)
    : SqlQueryImpl(query),
      m_db(db), m_bind(NULL), m_result(NULL)
{
    // allocate prepared statement object
    m_stmt = mysql_stmt_init(m_db.m_db);

    // prepare statement
    int rc = mysql_stmt_prepare(m_stmt, query.data(), query.size());

    if (rc != 0)
    {
        OUT_THROW("SQL query \"" << query << "\"\n" <<
                  "Failed : " << mysql_stmt_error(m_stmt));
    }

    execute();
}

//! Execute a SQL query with parameters, throws on errors.
MySqlQuery::MySqlQuery(class MySqlDatabase& db, const std::string& query,
                       const std::vector<std::string>& params)
    : SqlQueryImpl(query),
      m_db(db), m_bind(NULL), m_result(NULL)
{
    // allocate prepared statement object
    m_stmt = mysql_stmt_init(m_db.m_db);

    // prepare statement
    int rc = mysql_stmt_prepare(m_stmt, query.data(), query.size());

    if (rc != 0)
    {
        OUT_THROW("SQL query \"" << query << "\"\n" <<
                  "Failed : " << mysql_stmt_error(m_stmt));
    }

    // bind parameters
    MYSQL_BIND bind[params.size()];
    memset(bind, 0, sizeof(bind));

    for (size_t i = 0; i < params.size(); ++i)
    {
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = (char*)params[i].data();
        bind[i].buffer_length = params[i].size();
        bind[i].is_null = 0;
        bind[i].length = &bind[i].buffer_length;
    }

    rc = mysql_stmt_bind_param(m_stmt, bind);

    if (rc != 0)
    {
        OUT_THROW("SQL bind parameters " << query << "\n" <<
                  "Failed with " << mysql_stmt_error(m_stmt));
    }

    execute();
}

//! Free result
MySqlQuery::~MySqlQuery()
{
    mysql_stmt_close(m_stmt);

    if (m_bind)
        delete [] m_bind;

    if (m_result)
        delete [] m_result;
}

//! Bind output results and execute query
void MySqlQuery::execute()
{
    int rc = mysql_stmt_execute(m_stmt);

    if (rc != 0)
    {
        OUT_THROW("SQL execute \"" << query() << "\"\n" <<
                  "Failed : " << mysql_stmt_error(m_stmt));
    }

    //! Bind all result columns, mysql apparently cannot fetch single column
    //! data, mysql_stmt_fetch_column() segfaults without
    //! mysql_stmt_bind_result().
    int cols = num_cols();

    m_bind = new MySqlBind[cols];
    memset(m_bind, 0, cols * sizeof(MySqlBind));

    m_result = new MySqlColumn[cols];
    memset(m_result, 0, cols * sizeof(MySqlColumn));

    for (int c = 0; c < cols; ++c)
    {
        m_result[c].initialize_as_string(m_bind[c]);
    }

    mysql_stmt_bind_result(m_stmt, m_bind);

    m_row = -1;
}

//! Return number of rows in result, throws if no tuples.
unsigned int MySqlQuery::num_rows() const
{
    if (SqlDataCache::is_complete())
        return SqlDataCache::num_rows();

    assert(!"Row number not available without cached result.");
    return -1;
}

//! Return column name of col
std::string MySqlQuery::col_name(unsigned int col) const
{
    MYSQL_RES* res = mysql_stmt_result_metadata(m_stmt);

    MYSQL_FIELD *field = mysql_fetch_field_direct(res, col);

    std::string name = field->name;

    mysql_free_result(res);

    return name;
}

//! Return number of columns in result, throws if no tuples.
unsigned int MySqlQuery::num_cols() const
{
    return mysql_stmt_field_count(m_stmt);
}

//! Read column name map for the following col -> num mappings.
void MySqlQuery::read_colmap()
{
    m_colmap.clear();

    MYSQL_RES* res = mysql_stmt_result_metadata(m_stmt);

    MYSQL_FIELD *field;
    unsigned int col = 0;

    while ( (field = mysql_fetch_field(res)) )
    {
        m_colmap[ field->name ] = col++;
    }

    mysql_free_result(res);
}

//! Return the current row number
unsigned int MySqlQuery::current_row() const
{
    return m_row;
}

//! Advance current result row to next (or first if uninitialized)
bool MySqlQuery::step()
{
    ++m_row;
    return (mysql_stmt_fetch(m_stmt) == 0);
}

//! Returns true if cell (row,col) is NULL.
bool MySqlQuery::isNULL(unsigned int col) const
{
    assert(col < num_cols());
    return m_result[col].is_null;
}

//! Return text representation of column col of current row.
std::string MySqlQuery::text(unsigned int col) const
{
    assert(col < num_cols());
    return std::string(m_result[col].strdata, m_result[col].length);
}

//! read complete result into memory
void MySqlQuery::read_complete()
{
    return SqlDataCache::read_complete(*this);
}

//! Returns true if cell (row,col) is NULL.
bool MySqlQuery::isNULL(unsigned int row, unsigned int col) const
{
    return SqlDataCache::isNULL(row, col);
}

//! Return text representation of cell (row,col).
std::string MySqlQuery::text(unsigned int row, unsigned int col) const
{
    return SqlDataCache::text(row, col);
}

////////////////////////////////////////////////////////////////////////////////

//! try to connect to the database with default parameters
bool MySqlDatabase::initialize()
{
    // create mysql connection object
    m_db = mysql_init(NULL);

    if (!m_db) OUT_THROW("Could not create MySQL object.");

    // open connection to the database
    if (mysql_real_connect(m_db, NULL, NULL, NULL, NULL, 0, NULL, 0) == NULL)
    {
        OUT("Connection to MySQL database failed: " << errmsg());
        return false;
    }

    // have to select a database
    execute("USE test");

    return true;
}

//! destructor to free connection
MySqlDatabase::~MySqlDatabase()
{
    mysql_close(m_db);
}

//! return type of SQL database
MySqlDatabase::db_type MySqlDatabase::type() const
{
    return DB_MYSQL;
}

//! return string for the i-th placeholder, where i starts at 0.
std::string MySqlDatabase::placeholder(unsigned int) const
{
    return "?";
}

//! return quoted table or field identifier
std::string MySqlDatabase::quote_field(const std::string& field) const
{
    return '`' + field + '`';
}

//! execute SQL query without result
bool MySqlDatabase::execute(const std::string& query)
{
    // prepare statement
    int rc = mysql_real_query(m_db, query.data(), query.size());

    if (rc != 0)
    {
        OUT_THROW("SQL query \"" << query << "\"\n" <<
                  "Failed : " << errmsg());
    }

    return true;
}

//! construct query object for given string
SqlQuery MySqlDatabase::query(const std::string& query)
{
    return SqlQuery( new MySqlQuery(*this, query) );
}

//! construct query object for given string with placeholder parameters
SqlQuery MySqlDatabase::query(const std::string& query,
                              const std::vector<std::string>& params)
{
    return SqlQuery( new MySqlQuery(*this, query, params) );
}

//! test if a table exists in the database
bool MySqlDatabase::exist_table(const std::string& table)
{
    // in MySQL there is no way to check for existing TEMPORARY TABLES, so we
    // just DROP TABLE and retry CREATE TABLE if it fails onces.

    return false;
}

//! return last error message string
const char* MySqlDatabase::errmsg() const
{
    return mysql_error(m_db);
}

////////////////////////////////////////////////////////////////////////////////
