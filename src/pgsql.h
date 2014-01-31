/******************************************************************************
 * src/pgsql.h
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

#ifndef PGSQL_HEADER
#define PGSQL_HEADER

#include <libpq-fe.h>

#include "sql.h"

class PgSqlQuery : public SqlQueryImpl
{
protected:
    //! PostgreSQL database connection
    class PgSqlDatabase& m_db;

    //! PostgreSQL result object
    PGresult* m_res;

    //! Current result row
    unsigned int m_row;

public:
    
    //! Execute a SQL query without placeholders, throws on errors.
    PgSqlQuery(class PgSqlDatabase& db, const std::string& query);

    //! Execute a SQL query with placeholders, throws on errors.
    PgSqlQuery(class PgSqlDatabase& db, const std::string& query,
               const std::vector<std::string>& params);

    //! Free result
    ~PgSqlQuery();

    //! Return number of rows in result, throws if no tuples.
    unsigned int num_rows() const;

    //! Return number of columns in result, throws if no tuples.
    unsigned int num_cols() const;

    // *** Column Name Mapping ***

    //! Return column name of col
    std::string col_name(unsigned int col) const;

    // *** Result Iteration ***

    //! Return the current row number.
    unsigned int current_row() const;

    //! Advance current result row to next (or first if uninitialized)
    bool step();

    //! Returns true if cell (current_row,col) is NULL.
    bool isNULL(unsigned int col) const;

    //! Return text representation of column col of current row.
    std::string text(unsigned int col) const;

    // *** Complete Result Caching ***

    //! read complete result into memory
    void read_complete();

    //! Returns true if cell (row,col) is NULL.
    bool isNULL(unsigned int row, unsigned int col) const;

    //! Return text representation of cell (row,col).
    std::string text(unsigned int row, unsigned int col) const;
};

//! PostgreSQL database connection
class PgSqlDatabase : public SqlDatabase
{
protected:
    //! database connection
    PGconn* m_pg;

    //! for access to database connection
    friend class PgSqlQuery;

public:
    //! virtual destructor to free connection
    virtual ~PgSqlDatabase();

    //! return type of SQL database
    virtual db_type type() const;

    //! try to connect to the database with default parameters
    virtual bool initialize();

    //! return string for the i-th placeholder, where i starts at 0.
    virtual std::string placeholder(unsigned int i) const;

    //! return quoted table or field identifier
    virtual std::string quote_field(const std::string& field) const;

    //! execute SQL query without result
    virtual bool execute(const std::string& query);

    //! construct query object for given string
    virtual SqlQuery query(const std::string& query);

    //! construct query object for given string with placeholder parameters
    virtual SqlQuery query(const std::string& query,
                           const std::vector<std::string>& params);

    //! test if a table exists in the database
    virtual bool exist_table(const std::string& table);

    //! return last error message string
    virtual const char* errmsg() const;
};

#endif // PGSQL_HEADER
